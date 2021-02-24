/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2021 YADRO.
 */

#include "config.h"

#include "image_bios.hpp"

#include "dbus.hpp"
#include "fwupderr.hpp"
#include "subprocess.hpp"
#include "tracer.hpp"

#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <sys/ioctl.h>
#include <unistd.h>

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
static constexpr auto pca9698Bus = "/dev/i2c-11";
static constexpr auto pca9698Addr = 0x27;
static constexpr auto pca9698InputReg = 0x00;
static constexpr auto pca9698ModeReg = 0x2a;
static constexpr auto pca9698OEPolBit = 0x01;

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

/**
 * @brief Open I2C device file
 *
 * @param path - path to i2c bus file
 * @param addr - device address
 *
 * @return File descriptor of device file.
 */
static int openI2CDevice(const fs::path& path, uint8_t addr)
{
    int fd = open(path.c_str(), O_RDWR);
    if (fd < 0)
    {
        throw FwupdateError("Unable to open '%s', %s", path.c_str(),
                            strerror(errno));
    }

    if (ioctl(fd, I2C_SLAVE_FORCE, addr) < 0)
    {
        int err = errno;
        close(fd);
        throw FwupdateError("Unable to set I2C_SLAVE_FORCE, %s", strerror(err));
    }

    return fd;
}

/**
 * @brief General I2C SMBus commands wrapper
 *
 * @param fd        - device file descriptor
 * @param readWrite - i2c function
 * @param command   - i2c command
 * @param size      - data block size
 * @param data      - pointer to datat
 *
 * @return 0 on success, -1 on error.
 */
static inline int i2c_smbus_access(int fd, uint8_t readWrite, uint8_t command,
                                   uint32_t size, union i2c_smbus_data* data)
{
    struct i2c_smbus_ioctl_data args;

    args.read_write = readWrite;
    args.command = command;
    args.size = size;
    args.data = data;

    return ioctl(fd, I2C_SMBUS, &args);
}

/**
 * @brief Read byte from i2c device
 *
 * @param fd  - device file descriptor
 * @param reg - device register address
 *
 * @return register value
 */
static inline uint8_t i2c_smbus_read_byte_data(int fd, uint8_t reg)
{
    union i2c_smbus_data data;

    if (i2c_smbus_access(fd, I2C_SMBUS_READ, reg, I2C_SMBUS_BYTE_DATA, &data))
    {
        throw FwupdateError("I2C read failed, %s", strerror(errno));
    }

    return (0xFF & data.byte);
}

/**
 * @brief Write byte to i2c device
 *
 * @param fd    - device file descriptor
 * @param reg   - device register address
 * @param value - register value
 */
static inline void i2c_smbus_write_byte_data(int fd, uint8_t reg, uint8_t value)
{
    union i2c_smbus_data data;

    data.byte = value;

    if (i2c_smbus_access(fd, I2C_SMBUS_WRITE, reg, I2C_SMBUS_BYTE_DATA, &data))
    {
        throw FwupdateError("I2C write failed, %s", strerror(errno));
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

            pca9698FD = openI2CDevice(pca9698Bus, pca9698Addr);
            uint8_t input =
                i2c_smbus_read_byte_data(pca9698FD, pca9698InputReg);
            constexpr auto PCHPowerBit = (1 << 1);
            if (input & PCHPowerBit)
            {
                uint8_t mode =
                    i2c_smbus_read_byte_data(pca9698FD, pca9698ModeReg);
                mode |= pca9698OEPolBit;
                i2c_smbus_write_byte_data(pca9698FD, pca9698ModeReg, mode);
            }
            else
            {
                close(pca9698FD);
                pca9698FD = -1;
            }

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

            if (pca9698FD >= 0)
            {
                uint8_t mode =
                    i2c_smbus_read_byte_data(pca9698FD, pca9698ModeReg);
                if (mode & pca9698OEPolBit)
                {
                    mode &= ~pca9698OEPolBit;
                    i2c_smbus_write_byte_data(pca9698FD, pca9698ModeReg, mode);
                }

                close(pca9698FD);
                pca9698FD = -1;
            }

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
