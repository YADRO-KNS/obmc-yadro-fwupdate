/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 YADRO.
 */

#include "config.h"

#include "intel-platforms.hpp"

#include "dbus.hpp"
#include "subprocess.hpp"
#include "tracer.hpp"

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
    size_t bootaddr = IMAGE_A_ADDR;
    const char* mtd = "/dev/mtd/image-a";

    if (getBootAddress() == IMAGE_A_ADDR)
    {
        bootaddr = IMAGE_B_ADDR;
        mtd = "/dev/mtd/image-b";
    }

    // NOTE: This process may take a lot of time and we want to show the
    //       progress from original pflash output.
    fprintf(stdout, "Writing %s to %s\n", file.filename().c_str(), mtd);
    fflush(stdout);
    int rc = system(strfmt("flashcp -v %s %s", file.c_str(), mtd).c_str());
    checkWaitStatus(rc, "");

    setBootAddress(bootaddr);
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
    return file.filename() == "image-runtime";
}
