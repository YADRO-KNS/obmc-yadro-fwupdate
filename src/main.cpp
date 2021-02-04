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
 * @brief Prints version details of all active software objects.
 */
static void showVersion()
{
    auto tree = getSubTree(SOFTWARE_OBJPATH, {ACTIVATION_IFACE});
    for (auto& treeEntry : tree)
    {
        for (auto& busEntry : treeEntry.second)
        {
            auto activation =
                getProperty<std::string>(busEntry.first, treeEntry.first,
                                         ACTIVATION_IFACE, "Activation");
            if (activation != ACTIVATION_IFACE ".Activations.Active")
            {
                continue;
            }

            auto purpose = getProperty<std::string>(
                busEntry.first, treeEntry.first, VERSION_IFACE, "Purpose");
            auto version = getProperty<std::string>(
                busEntry.first, treeEntry.first, VERSION_IFACE, "Version");

            printf("%-6s  %s   [ID=%s]\n",
                   purpose.c_str() + purpose.rfind('.') + 1, version.c_str(),
                   treeEntry.first.c_str() + treeEntry.first.rfind('/') + 1);

            try
            {
                auto extVersion = getProperty<std::string>(
                    busEntry.first, treeEntry.first, EXTENDED_VERSION_IFACE,
                    "ExtendedVersion");
                size_t begin = 0;
                while (begin != std::string::npos)
                {
                    size_t end = extVersion.find(',', begin);
                    printf("        %s\n",
                           extVersion.substr(begin, end - begin).c_str());

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
    bool manualReboot = !interactive;

    if (interactive &&
        !confirm("The BMC system will be rebooted to apply changes."))
    {
        manualReboot = true;
    }

    try
    {
        if (!manualReboot)
        {
            Tracer tracer("Reboot BMC system");
            std::ignore = exec("/sbin/reboot");
            tracer.done();
        }
    }
    catch (...)
    {
        manualReboot = true;
    }

    if (manualReboot)
    {
        printf("The BMC needs to be manually rebooted.\n");
    }
}

/**
 * @brief Reset all settings to manufacturing default.
 *
 * @param interactive - flag to use interactive mode
 *                      (ask for user confirmation)
 * @param force       - flag to reset without locks
 */
void resetFirmware(bool interactive, bool force)
{
    constexpr auto LOST_DATA_WARN =
        "WARNING: "
        "All settings will be restored to manufacturing default values.";

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
 * @param firmwareFile  - Path to firmware file.
 * @param reset         - flag to drop current settings.
 * @param interactive   - flag to use interactive mode.
 * @param skipSignCheck - flag to skip signature verification.
 * @param force         - flag to flash without lock
 */
void flashFirmware(const fs::path& firmwareFile, bool reset, bool interactive,
                   bool skipSignCheck, bool force)
{
    if (!fs::exists(firmwareFile))
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
    fwupdate.unpack(firmwareFile);

    if (!skipSignCheck)
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
static void printUsage(const char* app)
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
    /* Disable buffering on stdout */
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("YADRO firmware updater ver %s\n", PROJECT_VERSION);

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
    bool doReset = false;
    bool skipSignCheck = false;
    bool forceFlash = false;
    bool doShowVersion = false;
    std::string firmwareFile;

    opterr = 0;
    int optVal;
    while ((optVal = getopt_long(argc, argv, "hf:rsFyv", opts, nullptr)) != -1)
    {
        switch (optVal)
        {
            case 'h':
                printUsage(argv[0]);
                return EXIT_SUCCESS;

            case 'f':
                firmwareFile = optarg;
                break;

            case 'r':
                doReset = true;
                break;

            case 's':
                skipSignCheck = true;
                break;

            case 'F':
                forceFlash = true;
                break;

            case 'y':
                interactive = false;
                break;

            case 'v':
                doShowVersion = true;
                break;

            default:
                fprintf(stderr, "Invalid option: %s\n", argv[optind - 1]);
                printUsage(argv[0]);
                return EXIT_FAILURE;
        }
    }

    try
    {
        if (doShowVersion)
        {
            showVersion();
        }
        else if (!firmwareFile.empty())
        {
            flashFirmware(firmwareFile, doReset, interactive, skipSignCheck,
                          forceFlash);
        }
        else if (doReset)
        {
            resetFirmware(interactive, forceFlash);
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
