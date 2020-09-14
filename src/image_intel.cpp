/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 YADRO.
 */

#include "config.h"

#include "image_intel.hpp"

#include "dbus.hpp"
#include "subprocess.hpp"
#include "tracer.hpp"

#include <sys/mount.h>
#include <unistd.h>

#include <fstream>
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

using FileSystem = std::string;
using MountPoint = std::string;
using MountPoints = std::vector<std::pair<FileSystem, MountPoint>>;

static MountPoints getMountPoints()
{
    /* This regexp gives MTD partitions and their mount points.
     * For example:
     *   mtd:rwfs /tmp/.rwfs jffs2 rw,sync,relatime 0 0
     *   mtd:sofs /var/sofs jffs2 rw,sync,relatime 0 0
     * give two pairs:
     *   {'mtd:rwfs', '/tmp/.rwfs'},
     *   {'mtd:sofs', '/var/sofs'}
     */
    static const std::regex MTDPartsTmpl("^(mtd:\\w+)\\s+([^\\s]+)\\s+.*$");
    MountPoints mtdPartitions;
    std::ifstream mounts("/proc/mounts");

    if (mounts.is_open())
    {
        std::string line;
        while (std::getline(mounts, line))
        {
            std::smatch match;
            if (std::regex_match(line, match, MTDPartsTmpl))
            {
                mtdPartitions.emplace_back(
                    std::make_pair(match[1].str(), match[2].str()));
            }
        }

        mounts.close();
    }

    return mtdPartitions;
}

static void unmountFilesystem(const std::string& filesystem)
{
    // Some systemd services may to occupate the RW partition
    // and make delay up to 20 seconds. We should to wait them
    // befor throw an error.
    constexpr auto maxTries = 40;  // Max number of tries to unmount
    constexpr auto delay = 500000; // Delay in microseconds between tries

    auto tryNumber = 0;

    while (true)
    {
        int rc = umount(filesystem.c_str());
        if (rc < 0 && errno == EBUSY && tryNumber < maxTries)
        {
            tryNumber++;
            usleep(delay);
        }
        else if (rc < 0)
        {
            throw FwupdateError("umount %s failed, error=%d: %s",
                                filesystem.c_str(), errno, strerror(errno));
        }
        else if (rc == 0)
        {
            break;
        }
    }
}
/**
 * @brief Stops all services which ones use the flash drive
 *        and unmount partitions from the flash drive.
 */
static void releaseFlashDrive()
{
    const std::vector<std::string> units = {
        "rotate-event-logs.timer", "rotate-event-logs.service",
        "logrotate.timer",         "logrotate.service",
        "nv-sync.service",
    };

    for (const auto& unit : units)
    {
        if (doesUnitExist(unit))
        {
            Tracer tracer("Stopping %s", unit.c_str());
            stopUnit(unit);
            tracer.done();
        }
    }

    auto mountPoints = getMountPoints();

    for (const auto& pt : mountPoints)
    {
        Tracer tracer("Unmounting %s", pt.first.c_str());
        unmountFilesystem(pt.second);
        tracer.done();
    }
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
