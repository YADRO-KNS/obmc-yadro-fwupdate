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
static size_t get_boot_address()
{
    const std::string bootcmd_prefix = "bootcmd=bootm";

    std::string bootcmd = exec("fw_printenv bootcmd");
    if (bootcmd.compare(0, bootcmd_prefix.length(), bootcmd_prefix) == 0)
    {
        return std::stoul(bootcmd.substr(bootcmd_prefix.length()), nullptr, 16);
    }

    return 0;
}

/**
 * @brief Set boot address for the next boot.
 */
static void set_boot_address(size_t address)
{
    std::ignore = exec("fw_setenv bootcmd bootm %08x", address);
}

/**
 * @brief Stops all services which ones use the flash drive
 *        and unmount partitions from the flash drive.
 */
static void release_flash_drive()
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
    size_t bootaddr = get_boot_address();

    release_flash_drive();

    Tracer tracer("Clear writable partitions");
    std::ignore = exec("flash_erase /dev/mtd/rwfs 0 0");
    std::ignore = exec("flash_erase /dev/mtd/sofs 0 0");
    std::ignore = exec("flash_erase /dev/mtd/u-boot-env 0 0");

    if (bootaddr == IMAGE_B_ADDR)
    {
        set_boot_address(bootaddr);
    }
    tracer.done();
}

void IntelPlatformsUpdater::do_install(const fs::path& file)
{
    size_t bootaddr = IMAGE_A_ADDR;
    const char* mtd = "/dev/mtd/image-a";

    if (get_boot_address() == IMAGE_A_ADDR)
    {
        bootaddr = IMAGE_B_ADDR;
        mtd = "/dev/mtd/image-b";
    }

    // NOTE: This process may take a lot of time and we want to show the
    //       progress from original pflash output.
    fprintf(stdout, "Writing %s to %s\n", file.filename().c_str(), mtd);
    fflush(stdout);
    int rc = system(strfmt("flashcp -v %s %s", file.c_str(), mtd).c_str());
    check_wait_status(rc, "");

    set_boot_address(bootaddr);
}

bool IntelPlatformsUpdater::do_after_install(bool reset)
{
    if (reset)
    {
        this->reset();
    }
    return true;
}

bool IntelPlatformsUpdater::is_file_belong(const fs::path& file) const
{
    return file.filename() == "image-runtime";
}
