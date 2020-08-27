/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 YADRO.
 */

#include "config.h"

#include "image_intel.hpp"

#include "dbus.hpp"
#include "subprocess.hpp"
#include "tracer.hpp"

#include <regex>

constexpr size_t IMAGE_A_ADDR = 0x20080000;
constexpr size_t IMAGE_B_ADDR = 0x22480000;

/**
 * @brief Get current boot address
 */
static size_t getBootAddress()
{
    const std::string bootcmdPrefix = "bootcmd=bootm";

    std::string bootcmd = exec("fw_printenv bootcmd");
    if (bootcmd.compare(0, bootcmdPrefix.length(), bootcmdPrefix) == 0)
    {
        return std::stoul(bootcmd.substr(bootcmdPrefix.length()), nullptr, 16);
    }

    return 0;
}

/**
 * @brief Set boot address for the next boot.
 */
static void setBootAddress(size_t address)
{
    std::ignore = exec("fw_setenv bootcmd bootm %08x", address);
}

/**
 * @brief Stops all services which ones use the flash drive
 *        and unmount partitions from the flash drive.
 */
static void releaseFlashDrive()
{
    Tracer tracer("Release flash drive");

    stopUnit("rotate-event-logs.timer");
    stopUnit("rotate-event-logs.service");
    stopUnit("logrotate.timer");
    stopUnit("logrotate.service");
    stopUnit("nv-sync.service");

    std::ignore = exec("umount mtd:rwfs");
    std::ignore = exec("umount mtd:sofs");

    tracer.done();
}

void IntelPlatformsUpdater::reset()
{
    size_t bootaddr = getBootAddress();

    releaseFlashDrive();

    Tracer tracer("Clear writable partitions");
    std::ignore = exec("flash_erase /dev/mtd/rwfs 0 0");
    std::ignore = exec("flash_erase /dev/mtd/sofs 0 0");
    std::ignore = exec("flash_erase /dev/mtd/u-boot-env 0 0");

    if (bootaddr == IMAGE_B_ADDR)
    {
        setBootAddress(bootaddr);
    }
    tracer.done();
}

void IntelPlatformsUpdater::doInstall(const fs::path& file)
{
    size_t bootaddr = 0;
    const char* mtd = nullptr;

    const std::string& filename = file.filename();

    if (filename == "image-mtd")
    {
        mtd = "/dev/mtd0";
        releaseFlashDrive();
    }
    else if (filename == "image-runtime")
    {
        switch (getBootAddress())
        {
            case IMAGE_A_ADDR:
                bootaddr = IMAGE_B_ADDR;
                mtd = "/dev/mtd/image-b";
                break;

            case IMAGE_B_ADDR:
                bootaddr = IMAGE_A_ADDR;
                mtd = "/dev/mtd/image-a";
                break;

            default:
                throw FwupdateError("Unable to determine boot address!");
                break;
        };
    }
    else if (filename == "image-u-boot")
    {
        if (fs::file_size(file) == 0)
        {
            // Typically image-u-boot present in the bundle as an empty file.
            // It is not an error and file should be skipped.
            return;
        }

        mtd = "/dev/mtd/u-boot";
    }

    if (mtd)
    {
        // NOTE: This process may take a lot of time and we want to show the
        //       progress from original pflash output.
        printf("Writing %s to %s\n", filename.c_str(), mtd);
        int rc = system(strfmt("flashcp -v %s %s", file.c_str(), mtd).c_str());
        checkWaitStatus(rc, "");
    }
    else
    {
        throw FwupdateError("No partition defined for %s", filename.c_str());
    }

    if (bootaddr)
    {
        setBootAddress(bootaddr);
    }
}

bool IntelPlatformsUpdater::doAfterInstall(bool reset)
{
    if (reset)
    {
        this->reset();
    }
    return true;
}

bool IntelPlatformsUpdater::isFileFlashable(const fs::path& file) const
{
    static const std::regex image("^image-(mtd|runtime|u-boot)$");
    return std::regex_match(file.filename().string(), image);
}
