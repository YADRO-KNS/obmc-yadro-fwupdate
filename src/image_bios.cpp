/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2021 YADRO.
 */

#include "config.h"

#include "dbus.hpp"
#include "fwupderr.hpp"
#include "image_bios.hpp"
#include "subprocess.hpp"
#include "tracer.hpp"

#include <chrono>
#include <fstream>
#include <thread>

static const fs::path aspeedSMC = "/sys/bus/platform/drivers/aspeed-smc";
static constexpr auto spiDriver = "1e631000.spi";
static const char* mtdDevice = "/dev/mtd/bios";
static const size_t mtdBlockSize = 0xffff;
static constexpr auto gpioNamePCHPower = "PWRGD_DSW_PWROK";
static constexpr auto gpioNameBIOSSel = "MIO_BIOS_SEL";
static constexpr auto gpioOwner = "fwupdate";
static constexpr size_t nvramOffset = 0x01000000;
static constexpr size_t nvramSize = 0x00080000;
static const char* nvramFile = "nvram.bin";

/**
 * @brief Check if SPI driver is already bound
 */
static bool isSPIDriverBound()
{
    return fs::exists(aspeedSMC / spiDriver);
}

/**
 * @brief Execute bind or unbind SPI driver using sysfs.
 */
static void bindOrUnbindSPIDriver(bool action)
{
    const std::string actionStr = action ? "bind" : "unbind";
    std::ofstream file;
    file.exceptions(std::ofstream::failbit | std::ofstream::badbit |
                    std::ofstream::eofbit);

    try
    {
        file.open(aspeedSMC / actionStr);
        file << spiDriver;
        file.close();
    }
    catch (const std::exception& e)
    {
        throw FwupdateError("Unable to %s SPI driver! %s", actionStr.c_str(),
                            e.what());
    }

    constexpr auto maxDriverBindingChecks = 100;
    constexpr auto delayBetweenChecksMS = 20;
    for (int i = 0; i < maxDriverBindingChecks; ++i)
    {
        if (isSPIDriverBound() == action)
        {
            // Operation completed.
            return;
        }

        std::this_thread::sleep_for(
            std::chrono::milliseconds(delayBetweenChecksMS));
    }

    throw FwupdateError("The SPI driver %s timed out!",
                        action ? "binding" : "unbinding");
}

static void bindSPIDriver()
{
    if (!isSPIDriverBound())
    {
        bindOrUnbindSPIDriver(true);
    }
}

static void unbindSPIDriver()
{
    if (isSPIDriverBound())
    {
        bindOrUnbindSPIDriver(false);
    }
}

/**
 * @brief Take control of specified GPIO line and set its direction to the
 *        output mode and wanted value.
 *
 * @param name     - GPIO line name
 * @param value    - Wanted value
 * @param gpioLine - Reference of GPIO line object
 */
void setGPIOOutput(const std::string& name, const int value,
                   gpiod::line& gpioLine)
{
    gpioLine = gpiod::find_line(name);
    if (!gpioLine)
    {
        throw FwupdateError("GPIO line %s not found!", name.c_str());
    }

    try
    {
        gpioLine.request({gpioOwner, gpiod::line_request::DIRECTION_OUTPUT, 0},
                         value);
    }
    catch (const std::exception& e)
    {
        throw FwupdateError("Unable to set GPIO %s! %s", name.c_str(),
                            e.what());
    }
}

/**
 * @brief Release GPIO line and restore default state.
 */
static void releaseGPIO(gpiod::line& gpioLine)
{
    if (gpioLine)
    {
        try
        {
            gpioLine.release();
            gpioLine.request(
                {gpioOwner, gpiod::line_request::DIRECTION_INPUT, 0});
        }
        catch (const std::exception& e)
        {
            throw FwupdateError("Unable to release GPIO %s! %s",
                                gpioLine.name().c_str(), e.what());
        }

        gpioLine.reset();
    }
}

void BIOSUpdater::lock()
{
    if (!files.empty())
    {
        if (isChassisOn())
        {
            throw FwupdateError(
                "The host is running now, operation cancelled!");
        }

        {
            Tracer tracer("Shutting down PCH");
            setGPIOOutput(gpioNamePCHPower, 0, gpioPCHPower);

            // The GPIO is marked as used by this process.
            locked = true;

            tracer.done();
        }

        {
            Tracer tracer("Get access to BIOS flash");

            // This makes the BMC to reinit the driver which might be attached
            // with wrong data on the hosts equipped with em100.
            unbindSPIDriver();

            setGPIOOutput(gpioNameBIOSSel, 1, gpioBIOSSel);
            bindSPIDriver();
            tracer.done();
        }
    }
}

void BIOSUpdater::unlock()
{
    if (locked)
    {
        {
            Tracer tracer("Return back BIOS flash control to host");
            unbindSPIDriver();
            releaseGPIO(gpioBIOSSel);
            tracer.done();
        }
        {
            Tracer tracer("Restoring PCH power");
            releaseGPIO(gpioPCHPower);

            // Wait for ME booting
            std::this_thread::sleep_for(std::chrono::seconds(10));

            // The GPIO is released.
            locked = false;
            tracer.done();
        }
    }
}

void BIOSUpdater::doInstall(const fs::path& file)
{
    // NOTE: This process may take a lot of time and we want to show the
    //       progress from original flashcp output.
    printf("Writing %s to %s\n", file.filename().c_str(), mtdDevice);
    int rc =
        system(strfmt(FLASHCP_CMD " -v %s %s", file.c_str(), mtdDevice).c_str());
    checkWaitStatus(rc, "");
}

void BIOSUpdater::doBeforeInstall(bool reset)
{
    if (!reset)
    {
        puts("Preserving UEFI settings...");
        const fs::path dumpFile(tmpdir / nvramFile);
        const std::string cmd =
            strfmt(NANDDUMP_CMD " --startaddress %lu --length %lu --file '%s' %s",
                   nvramOffset, nvramSize, dumpFile.c_str(), mtdDevice);
        const int rc = system(cmd.c_str());
        checkWaitStatus(rc, std::string());
        if (!fs::exists(dumpFile))
        {
            throw FwupdateError("Error reading NVRAM");
        }
    }
}

bool BIOSUpdater::doAfterInstall(bool reset)
{
    if (!reset)
    {
        const fs::path dumpFile(tmpdir / nvramFile);
        if (!fs::exists(dumpFile))
        {
            throw FwupdateError("NVRAM dump not found");
        }

        puts("Preparing NVRAM partition...");
        const std::string cmdErase =
            strfmt(FLASH_ERASE_CMD " %s %lu %lu", mtdDevice, nvramOffset,
                   nvramSize / mtdBlockSize);
        int rc = system(cmdErase.c_str());
        checkWaitStatus(rc, std::string());

        puts("Restoring UEFI settings...");
        const std::string cmdWrite =
            strfmt(NANDWRITE_CMD " --start %lu %s %s", nvramOffset, mtdDevice,
                   dumpFile.c_str());
        rc = system(cmdWrite.c_str());
        checkWaitStatus(rc, std::string());
    }
    return false; // reboot is not needed
}

bool BIOSUpdater::isFileFlashable(const fs::path& file) const
{
    return file.stem() == "vegman" &&
           (file.extension() == ".bin" || file.extension() == ".img");
}
