/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 YADRO.
 */

#include "config.h"

#include "confirm.hpp"
#include "dbus.hpp"
#include "fwupdate.hpp"
#include "fwupderr.hpp"
#include "subprocess.hpp"
#include "tracer.hpp"

#include <getopt.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

/**
 * @brief Prints version details of all avtive software objects.
 */
static void show_version()
{
    auto tree = getSubTree(SOFTWARE_OBJPATH, {ACTIVATION_IFACE});
    for (auto& tree_entry : tree)
    {
        for (auto& bus_entry : tree_entry.second)
        {
            auto activation =
                getProperty<std::string>(bus_entry.first, tree_entry.first,
                                         ACTIVATION_IFACE, "Activation");
            if (activation != ACTIVATION_IFACE ".Activations.Active")
            {
                continue;
            }

            auto purpose = getProperty<std::string>(
                bus_entry.first, tree_entry.first, VERSION_IFACE, "Purpose");
            auto version = getProperty<std::string>(
                bus_entry.first, tree_entry.first, VERSION_IFACE, "Version");

            printf("%-6s  %s   [ID=%s]\n",
                   purpose.c_str() + purpose.rfind('.') + 1, version.c_str(),
                   tree_entry.first.c_str() + tree_entry.first.rfind('/') + 1);

            try
            {
                auto ext_version = getProperty<std::string>(
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

/**
 * @brief Reboot the BMC.
 */
void reboot(bool interactive)
{
    bool manual_reboot = !interactive;

    if (interactive &&
        !confirm("The BMC system will be rebooted to apply changes."))
    {
        manual_reboot = true;
    }

    try
    {
        if (!manual_reboot)
        {
            Tracer tracer("Reboot BMC system");
            std::ignore = exec("/sbin/reboot");
            tracer.done();
        }
    }
    catch (...)
    {
        manual_reboot = true;
    }

    if (manual_reboot)
    {
        throw FwupdateError("The BMC needs to be manually rebooted.");
    }
}

/**
 * @brief Reset all settings to manufacturing default.
 *
 * @param interactive - flag to use interactive mode
 *                      (ask for user confirmation)
 * @param force       - flag to reset without locks
 */
void reset_firmware(bool interactive, bool force)
{
    constexpr auto LOST_DATA_WARN =
        "WARNING: "
        "All settings will be resotred to manufacturing default values.";

    if (interactive && !confirm(LOST_DATA_WARN))
    {
        return;
    }

    FwUpdate fwupdate(force);

    fwupdate.reset();

    reboot(interactive);
}

/**
 * @brief Flash the firmware files.
 *
 * @param firmware_file   - Path to firmware file.
 * @param reset           - flag to drop current settings.
 * @param interactive     - flag to use interactive mode.
 * @param skip_sign_check - flag to skip signature verification.
 * @param force           - flag to flash without lock
 */
void flash_firmware(const fs::path& firmware_file, bool reset, bool interactive,
                    bool skip_sign_check, bool force)
{
    if (!fs::exists(firmware_file))
    {
        throw FwupdateError("Firmware package not found!");
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

    FwUpdate fwupdate(force);
    fwupdate.unpack(firmware_file);

    if (!skip_sign_check)
    {
        fwupdate.verify();
    }

    if (fwupdate.install(reset))
    {
        reboot(interactive);
    }
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
                    the PNOR flash (such as NVRAM, GUARD, HBEL etc)
  -s, --no-sign     disable digital signature verification
  -F, --force       forced flash/reset firmware
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
        { "force",   no_argument,       0, 'F' },
        { "yes",     no_argument,       0, 'y' },
        { "version", no_argument,       0, 'v' },
        { 0,         0,                 0,  0  }
        // clang-format on
    };

    bool interactive = true;
    bool do_reset = false;
    bool skip_sign_check = false;
    bool force_flash = false;
    bool do_show_version = false;
    std::string firmware_file;

    opterr = 0;
    int opt_val;
    while ((opt_val = getopt_long(argc, argv, "hf:rsFyv", opts, nullptr)) != -1)
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

            case 'F':
                force_flash = true;
                break;

            case 'y':
                interactive = false;
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
            flash_firmware(firmware_file, do_reset, interactive,
                           skip_sign_check, force_flash);
        }
        else if (do_reset)
        {
            reset_firmware(interactive, force_flash);
        }
        else
        {
            fprintf(stderr, "One or both of --file/--reset "
                            "options must be specified!\n");
            return EXIT_FAILURE;
        }
    }
    catch (const std::exception& err)
    {
        fprintf(stderr, "%s\n", err.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
