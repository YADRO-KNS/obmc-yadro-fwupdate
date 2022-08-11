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

enum class BootPart
{
    image_a,
    image_b,
    unknown
};

/**
 * @brief Get current boot partition
 */
static BootPart getBootPart()
{
    BootPart activePart = BootPart::unknown;
    const std::string bootcmdPrefix = "bootcmd=bootm";
    std::string bootpart;

    // exec call trows exception when command return non-zero status
    try
    {
        auto bootpart = exec(FW_PRINTENV_CMD " bootside 2>/dev/null");
        return (bootpart == "bootside=b") ? BootPart::image_b
                                          : BootPart::image_a;
    }
    catch (...)
    {
        // no bootside variable defined, it is expected case
    }

    std::string bootcmd = exec(FW_PRINTENV_CMD " bootcmd 2>/dev/null");
    if (bootcmd.compare(0, bootcmdPrefix.length(), bootcmdPrefix) == 0)
    {
        size_t address =
            std::stoul(bootcmd.substr(bootcmdPrefix.length()), nullptr, 16);
        switch (address)
        {
            case IMAGE_A_ADDR:
                activePart = BootPart::image_a;
                break;
            case IMAGE_B_ADDR:
                activePart = BootPart::image_b;
                break;
        }
    }

    return activePart;
}

/**
 * @brief Set boot partition for the next boot.
 */
static void setBootPart(BootPart part)
{
    // exec call trows exception when command return non-zero status
    try
    {
        std::ignore = exec(FW_PRINTENV_CMD " -n bootside 2>/dev/null");
        std::ignore = exec(FW_SETENV_CMD " bootside %s 2>/dev/null",
                           (part == BootPart::image_b) ? "b" : "a");
    }
    catch (...)
    {
        // no bootside variable defined, fallback to direct bootm way
        size_t address =
            (part == BootPart::image_b) ? IMAGE_B_ADDR : IMAGE_A_ADDR;
        std::ignore =
            exec(FW_SETENV_CMD " bootcmd bootm %08x 2>/dev/null", address);
    }
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
    // Some systemd services may occupy the RW partition
    // resulting in a delay of up to 20 seconds. We should wait for them
    // before throwing an error.
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
    BootPart bootpart = getBootPart();

    releaseFlashDrive();

    Tracer tracer("Clear writable partitions");
    std::ignore = exec(FLASH_ERASE_CMD " /dev/mtd/rwfs 0 0");
    std::ignore = exec(FLASH_ERASE_CMD " /dev/mtd/sofs 0 0");
    std::ignore = exec(FLASH_ERASE_CMD " /dev/mtd/u-boot-env 0 0");

    if (bootpart == BootPart::image_b)
    {
        setBootPart(bootpart);
    }
    tracer.done();
}

void IntelPlatformsUpdater::lock()
{
    if (isChassisOn())
    {
        throw FwupdateError("The host is running now, operation cancelled!");
    }
}

void IntelPlatformsUpdater::doInstall(const fs::path& file)
{
    BootPart bootpart = BootPart::unknown;
    const char* mtd = nullptr;

    const std::string& filename = file.filename();

    if (filename == "image-mtd")
    {
        mtd = "/dev/mtd0";
        releaseFlashDrive();
    }
    else if (filename == "image-runtime")
    {
        switch (getBootPart())
        {
            case BootPart::image_a:
                bootpart = BootPart::image_b;
                mtd = "/dev/mtd/image-b";
                break;

            case BootPart::image_b:
                bootpart = BootPart::image_a;
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
        //       progress from original flashcp output.
        printf("Writing %s to %s\n", filename.c_str(), mtd);
        int rc = system(strfmt(FLASHCP_CMD " -v %s %s", file.c_str(), mtd).c_str());
        checkWaitStatus(rc, "");
    }
    else
    {
        throw FwupdateError("No partition defined for %s", filename.c_str());
    }

    if (bootpart != BootPart::unknown)
    {
        setBootPart(bootpart);
    }
}

bool IntelPlatformsUpdater::doAfterInstall(bool reset)
{
    if (reset)
    {
        this->reset();
    }
    // Reboot is required when the BMC has been updated only.
    return !files.empty();
}

bool IntelPlatformsUpdater::isFileFlashable(const fs::path& file) const
{
    static const std::regex image("^image-(mtd|runtime|u-boot)$");
    return std::regex_match(file.filename().string(), image);
}
