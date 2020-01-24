/**
 * @brief Application entry point.
 *
 * This file is part of OpenBMC/OpenPOWER firmware updater.
 *
 * Copyright 2020 YADRO
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "config.h"

#include "openbmc/firmware.hpp"
#include "utils/confirm.hpp"
#include "utils/dbus.hpp"
#include "utils/subprocess.hpp"
#include "utils/tags.hpp"
#include "utils/tracer.hpp"

#ifdef OPENPOWER_SUPPORT
#include "openpower/firmware.hpp"
#endif

#include <getopt.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

/**
 * @brief Prints version details of all avtive software objects.
 */
static void show_version()
{
    auto tree = utils::getSubTree(SOFTWARE_OBJPATH, {ACTIVATION_IFACE});
    for (auto& tree_entry : tree)
    {
        for (auto& bus_entry : tree_entry.second)
        {
            auto activation = utils::getProperty<std::string>(
                bus_entry.first, tree_entry.first, ACTIVATION_IFACE,
                "Activation");
            if (activation != ACTIVATION_IFACE ".Activations.Active")
            {
                continue;
            }

            auto purpose = utils::getProperty<std::string>(
                bus_entry.first, tree_entry.first, VERSION_IFACE, "Purpose");
            auto version = utils::getProperty<std::string>(
                bus_entry.first, tree_entry.first, VERSION_IFACE, "Version");

            printf("%-6s  %s   [ID=%s]\n",
                   purpose.c_str() + purpose.rfind('.') + 1, version.c_str(),
                   tree_entry.first.c_str() + tree_entry.first.rfind('/') + 1);

            try
            {
                auto ext_version = utils::getProperty<std::string>(
                    bus_entry.first, tree_entry.first, EXTENDED_VERSION_IFACE,
                    "ExtendedVersion");
                size_t begin = 0;
                while (begin != std::string::npos)
                {
                    size_t end = ext_version.find(',', begin);
                    printf("        %s\n",
                           ext_version.substr(begin, end - begin).c_str());

                    if (end != std::string::npos)
                    {
                        end++;
                    }
                    begin = end;
                }
            }
            catch (const std::runtime_error&)
            {
                // NOTE: Only PNOR contains the Extended Version field.
                continue;
            }
        }
    }
}

struct FirmwareLock
{
    FirmwareLock()
    {
        openbmc::lock();

        try
        {
#ifdef OPENPOWER_SUPPORT
            openpower::lock();
#endif
        }
        catch (...)
        {
            openbmc::unlock();
            std::rethrow_exception(std::current_exception());
        }
    }

    ~FirmwareLock()
    {
#ifdef OPENPOWER_SUPPORT
        openpower::unlock();
#endif
        openbmc::unlock();
    }

    FirmwareLock(const FirmwareLock&) = delete;
    FirmwareLock& operator=(const FirmwareLock&) = delete;
};

/**
 * @brief Reset all settings to manufacturing default.
 *
 * @param interactive - flag to use interactive mode
 *                      (ask for user confirmation)
 */
void reset_firmware(bool interactive)
{
    constexpr auto LOST_DATA_WARN =
        "WARNING: "
        "All settings will be resotred to manufacturing default values.";

    if (interactive && !utils::confirm(LOST_DATA_WARN))
    {
        return;
    }

    {
        FirmwareLock lock;
#ifdef OPENPOWER_SUPPORT
        openpower::reset();
#endif
        openbmc::reset();
    }

    openbmc::reboot(interactive);
}

/**
 * @brief Auto removable path object.
 */
struct RemovablePath : public fs::path
{
    using fs::path::path;

    // Copy operations are disallowed
    RemovablePath(const RemovablePath&) = delete;
    RemovablePath& operator=(const RemovablePath&) = delete;

    // Move operations are allowed
    RemovablePath(RemovablePath&&) = default;
    RemovablePath& operator=(RemovablePath&&) = default;

    // Recursive delete filesystem entries
    ~RemovablePath()
    {
        if (!empty())
        {
            std::error_code ec;
            fs::remove_all(*this, ec);
        }
    }
};

/**
 * @brief Flash the firmware files.
 *
 * @param firmware_file   - Path to firmware file.
 * @param reset           - flag to drop current settings.
 * @param interactive     - flag to use interactive mode.
 * @param skip_sign_check - flag to skip signature verification.
 */
void flash_firmware(const std::string& firmware_file, bool reset,
                    bool interactive, bool skip_sign_check)
{
    fs::path fn(firmware_file);
    if (!fs::exists(fn))
    {
        throw std::runtime_error("Firmware package not found!");
    }

    if (interactive)
    {
        std::string title("WARNING: Firmware will be updated.\n");

        if (reset)
        {
            title += "All settings will be restored to manufacture default "
                     "values.\n";
        }

        title += "Please do not turn off the system during update!";

        if (!utils::confirm(title.c_str()))
        {
            return;
        }
    }

    RemovablePath tmpDir;
    utils::tracer::trace_task("Prepare temporary directory", [&tmpDir]() {
        std::string dir = fs::temp_directory_path() / "fwupdateXXXXXX";
        if (!mkdtemp(dir.data()))
        {
            throw std::system_error(errno, std::generic_category());
        }

        tmpDir = dir;
    });

#ifdef OPENPOWER_SUPPORT
    if (!interactive && skip_sign_check && fn.extension() == PNOR_FILE_EXT)
    {
        // NOTE: This way is used by openpower-pnor-update@.service
        //       instead directly call pflash
        openpower::flash({fn}, reset ? "" : tmpDir.string());
        return;
    }
#endif

    utils::tracer::trace_task("Unpack firmware package", [fn, &tmpDir]() {
        int rc;
        std::tie(rc, std::ignore) = utils::subprocess::exec(
            "tar -xzf", fn, "-C", tmpDir, " 2>/dev/null");
        utils::subprocess::check_wait_status(rc);
    });

    auto manifestFile(tmpDir / MANIFEST_FILE_NAME);
    if (!fs::exists(manifestFile))
    {
        throw std::runtime_error("No MANIFEST file found!");
    }

    auto purpose = utils::get_tag_value(manifestFile, "purpose");
    // Cut off `xyz.openbmc_poroject.Software.Version.VersionPurpose.`
    purpose = purpose.substr(purpose.rfind('.') + 1);

    constexpr auto SystemPurpose = "System";
    constexpr auto HostPurpose = "Host";
    constexpr auto BmcPurpose = "BMC";

    // TODO: check signature

    {
        FirmwareLock lock;

#ifdef OPENPOWER_SUPPORT
        if (purpose == SystemPurpose || purpose == HostPurpose)
        {
            utils::tracer::trace_task("Flashing the OpenPOWER firmware",
                                      [&tmpDir, reset]() {
                                          // TODO: Flash OpenPOWER firmware
                                      });
        }
#endif

        if (purpose == SystemPurpose || purpose == BmcPurpose)
        {
            utils::tracer::trace_task("Flashing the OpenBMC firmware",
                                      [&tmpDir, reset]() {
                                          // TODO: Flash OpenBMC firmware
                                      });
        }
    }

    openbmc::reboot(interactive);
}

/**
 * @brief Print usage
 *
 * @param app - Application name
 */
static void print_usage(const char* app)
{
    printf("\nUsage: %s [-h] [-f FILE] [-r] [-s] [-y] [-v]\n", app);
    printf(R"(optional arguments:
  -h, --help        show this help message and exit
  -f, --file FILE   path to the firmware file
  -r, --reset       reset all settings to manufacturing default, this option
                    can be combined with -f or used as standalone command to
                    reset RW partition of OpenBMC and clean some partitions of
                    the PNOR flash (such as NVRAM, GUARD, HBEL etc).
  -s, --no-sign     disable digital signature verification
  -y, --yes         don't ask user for confirmation
  -v, --version     print installed firmware version info and exit
)");
}

/**
 * @brief Application entry point
 *
 * @param argc   - Arguments number
 * @param argv[] - Arguments list
 *
 * @return exit code
 */
int main(int argc, char* argv[])
{
    printf("OpenBMC/OpenPOWER firmware updater ver %s\n", PROJECT_VERSION);

    const struct option opts[] = {
        // clang-format off
        { "help",    no_argument,       0, 'h' },
        { "file",    required_argument, 0, 'f' },
        { "reset",   no_argument,       0, 'r' },
        { "no-sign", no_argument,       0, 's' },
        { "yes",     no_argument,       0, 'y' },
        { "version", no_argument,       0, 'v' },
        { 0,         0,                 0,  0  }
        // clang-format on
    };

    bool force_yes = false;
    bool do_reset = false;
    bool skip_sign_check = false;
    bool do_show_version = false;
    std::string firmware_file;

    opterr = 0;
    int opt_val;
    while ((opt_val = getopt_long(argc, argv, "hf:rsyv", opts, nullptr)) != -1)
    {
        switch (opt_val)
        {
            case 'h':
                print_usage(argv[0]);
                return EXIT_SUCCESS;

            case 'f':
                firmware_file = optarg;
                break;

            case 'r':
                do_reset = true;
                break;

            case 's':
                skip_sign_check = true;
                break;

            case 'y':
                force_yes = true;
                break;

            case 'v':
                do_show_version = true;
                break;

            default:
                fprintf(stderr, "Invalid option: %s\n", argv[optind - 1]);
                print_usage(argv[0]);
                return EXIT_FAILURE;
        }
    }

    try
    {
        if (do_show_version)
        {
            show_version();
        }
        else if (!firmware_file.empty())
        {
            flash_firmware(firmware_file, do_reset, !force_yes,
                           skip_sign_check);
        }
        else if (do_reset)
        {
            reset_firmware(!force_yes);
        }
        else
        {
            throw std::runtime_error(
                "One or both of --file/--reset options must be specified!");
        }
    }
    catch (const std::exception& err)
    {
        fprintf(stderr, "%s\n", err.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
