/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 YADRO.
 */

#include "config.h"

#include "confirm.hpp"
#include "dbus.hpp"
#include "openbmc.hpp"
#include "signature.hpp"
#include "subprocess.hpp"
#include "tags.hpp"
#include "tracer.hpp"

#ifdef OPENPOWER_SUPPORT
#include "openpower.hpp"
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
    auto tree = dbus::getSubTree(SOFTWARE_OBJPATH, {ACTIVATION_IFACE});
    for (auto& tree_entry : tree)
    {
        for (auto& bus_entry : tree_entry.second)
        {
            auto activation = dbus::getProperty<std::string>(
                bus_entry.first, tree_entry.first, ACTIVATION_IFACE,
                "Activation");
            if (activation != ACTIVATION_IFACE ".Activations.Active")
            {
                continue;
            }

            auto purpose = dbus::getProperty<std::string>(
                bus_entry.first, tree_entry.first, VERSION_IFACE, "Purpose");
            auto version = dbus::getProperty<std::string>(
                bus_entry.first, tree_entry.first, VERSION_IFACE, "Version");

            printf("%-6s  %s   [ID=%s]\n",
                   purpose.c_str() + purpose.rfind('.') + 1, version.c_str(),
                   tree_entry.first.c_str() + tree_entry.first.rfind('/') + 1);

            try
            {
                auto ext_version = dbus::getProperty<std::string>(
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

    if (interactive && !confirm(LOST_DATA_WARN))
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
 * @brief Verify the MANIFEST and publickey file using available public keys
 *        and hash on the system.
 *
 * @param firmwareDir - path to direcotry where the firmware package extracted.
 *
 * @return true if signature verification was successful, false otherwise.
 */
bool system_level_verify(const fs::path& firmwareDir)
{
    auto manifestFile(firmwareDir / MANIFEST_FILE_NAME);
    auto publicKeyFile(firmwareDir / PUBLICKEY_FILE_NAME);

    if (!fs::exists(manifestFile) || !fs::exists(publicKeyFile))
    {
        return false;
    }

    bool valid = false;
    try
    {
        // Verify the file signature with available public keys and hash
        // function. For any internal failure during the key/hash pair specific
        // validation, should continue the validation with next available
        // key/hash pair.
        for (const auto& p : fs::directory_iterator(SIGNED_IMAGE_CONF_PATH))
        {
            auto publicKey(p.path() / PUBLICKEY_FILE_NAME);
            auto hashFunc =
                get_tag_value(p.path() / HASH_FILE_NAME, "HashType");

            try
            {
                valid = verify_file(publicKey, hashFunc, manifestFile);
                if (valid)
                {
                    valid = verify_file(publicKey, hashFunc, publicKeyFile);
                    if (valid)
                    {
                        break;
                    }
                }
            }
            catch (...)
            {
                valid = false;
            }
        }
    }
    catch (const fs::filesystem_error&)
    {
        valid = false;
    }

    return valid;
}

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

        if (!confirm(title.c_str()))
        {
            return;
        }
    }

    RemovablePath tmpDir;
    tracer::trace_task("Prepare temporary directory", [&tmpDir]() {
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

    tracer::trace_task("Unpack firmware package", [fn, &tmpDir]() {
        int rc;
        std::tie(rc, std::ignore) =
            subprocess::exec("tar -xzf", fn, "-C", tmpDir, " 2>/dev/null");
        subprocess::check_wait_status(rc);
    });

    auto manifestFile(tmpDir / MANIFEST_FILE_NAME);
    if (!fs::exists(manifestFile))
    {
        throw std::runtime_error("No MANIFEST file found!");
    }

    auto purpose = get_tag_value(manifestFile, "purpose");
    // Cut off `xyz.openbmc_poroject.Software.Version.VersionPurpose.`
    purpose = purpose.substr(purpose.rfind('.') + 1);

    constexpr auto SystemPurpose = "System";
    constexpr auto HostPurpose = "Host";
    constexpr auto BmcPurpose = "BMC";

    if (!skip_sign_check)
    {
        tracer::trace_task("Check signature of firmware package", [&tmpDir]() {
            if (!system_level_verify(tmpDir))
            {
                throw std::runtime_error("System level verification failed!");
            }
        });

        // Check target machine type
        auto currentMachine =
            get_tag_value(OS_RELEASE_FILE, "OPENBMC_TARGET_MACHINE");
        if (currentMachine.empty())
        {
            // We are running on an old BMC version.
            fprintf(stdout, "WARNING: Current machine name is undefined, "
                            "the check is skipped.\n");
        }
        else
        {
            tracer::trace_task(
                "Check target machine type",
                [&manifestFile, &currentMachine]() {
                    auto targetMachine =
                        get_tag_value(manifestFile, "MachineName");
                    if (currentMachine != targetMachine)
                    {
                        throw std::runtime_error(
                            "Frimware package is not compatible with this "
                            "system.");
                    }
                });
        }

        auto publickeyFile(tmpDir / PUBLICKEY_FILE_NAME);
        auto hashFunc = get_tag_value(manifestFile, "HashType");

        if (purpose == SystemPurpose || purpose == BmcPurpose)
        {
            tracer::trace_task(
                "Checking signature of OpenBMC firwmare",
                [&tmpDir, &publickeyFile, &hashFunc]() {
                    for (const auto& entry : openbmc::get_fw_files(tmpDir))
                    {
                        if (!verify_file(publickeyFile, hashFunc, entry))
                        {
                            throw std::runtime_error(concat_string(
                                "Verification of", entry, "failed!"));
                        }
                    }
                });
        }

#ifdef OPENPOWER_SUPPORT
        if (purpose == SystemPurpose || purpose == HostPurpose)
        {
            tracer::trace_task(
                "Checking signature of OpenPOWER firwmare",
                [&tmpDir, &publickeyFile, &hashFunc]() {
                    for (const auto& entry : openpower::get_fw_files(tmpDir))
                    {
                        if (!verify_file(publickeyFile, hashFunc, entry))
                        {
                            throw std::runtime_error(concat_string(
                                "Verification of", entry, "failed!"));
                        }
                    }
                });
        }
#endif
    }

    {
        FirmwareLock lock;

#ifdef OPENPOWER_SUPPORT
        if (purpose == SystemPurpose || purpose == HostPurpose)
        {
            openpower::flash(openpower::get_fw_files(tmpDir),
                             reset ? "" : tmpDir.string());
        }
#endif

        if (purpose == SystemPurpose || purpose == BmcPurpose)
        {
            openbmc::flash(openbmc::get_fw_files(tmpDir), reset);
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
