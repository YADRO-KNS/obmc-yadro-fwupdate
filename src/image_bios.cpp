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
static constexpr auto gpioNamePCHPower = "PWRGD_DSW_PWROK";
static constexpr auto gpioNameBIOSSel = "MIO_BIOS_SEL";
static constexpr auto gpioOwner = "fwupdate";
static constexpr auto pca9698Bus = "/dev/i2c-11";
static constexpr auto pca9698Addr = 0x27;
static constexpr auto pca9698InputReg = 0x00;
static constexpr auto pca9698ModeReg = 0x2a;
static constexpr auto pca9698OEPolBit = 0x01;
static constexpr size_t nvramOffset = 0x01000000;
static constexpr size_t nvramSize = 0x00080000;
static const char* nvramFile = "nvram.bin";
static constexpr size_t gbeOffset = 0x00a36000;
static constexpr size_t gbeSize = 0x005ba000;
static const char* gbeFile = "gbe.bin";
static constexpr size_t ddBlockSize = 512;

// D-Bus interface and object to access to UEFI variables storage
static const char* uefivarInterface = "xyz.openbmc_project.UefiVar";
static const char* uefivarObject = "/xyz/openbmc_project/uefivar";

bool BIOSUpdater::writeGbeOnly = false;

/**
 * @brief Check if SPI driver is bound and MTD device mounted
 */
static bool isSPIDriverBound()
{
    return fs::exists(aspeedSMC / spiDriver) && fs::exists(mtdDevice);
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
    if (writeGbeOnly)
    {
        // mtd-util doesn't work with symlinks
        const fs::path mtdDeviceReal = fs::canonical(
            fs::path(mtdDevice).parent_path() / fs::read_symlink(mtdDevice));

        puts("Writing GBE...");

        const fs::path partFile(tmpdir / gbeFile);
        std::string cmd = strfmt(
            "dd if=%s of=%s skip=%lu count=%lu", file.c_str(), partFile.c_str(),
            gbeOffset / ddBlockSize, gbeSize / ddBlockSize);
        int rc = system(cmd.c_str());
        checkWaitStatus(rc, std::string());

        cmd = strfmt("mtd-util -d %s cp %s 0x%x", mtdDeviceReal.c_str(),
                     partFile.c_str(), gbeOffset);
        rc = system(cmd.c_str());
        checkWaitStatus(rc, std::string());
    }
    else
    {
        printf("Writing %s to %s\n", file.filename().c_str(), mtdDevice);
        int rc = system(
            strfmt(FLASHCP_CMD " -v %s %s", file.c_str(), mtdDevice).c_str());
        checkWaitStatus(rc, "");
    }
}

void BIOSUpdater::doBeforeInstall(bool reset)
{
    if (writeGbeOnly)
    {
        return;
    }

    if (!reset)
    {
        // Migrating UEFI settings from previous version (1.3 or earlier)
        puts("Import UEFI settings...");
        bool doSettingsExist = false;
        try
        {
            // Check if uefivar service still doesn't have settings
            auto method =
                systemBus.new_method_call(uefivarInterface, uefivarObject,
                                          uefivarInterface, "NextVariable");
            method.append(std::string(), std::vector<uint8_t>(16, 0));
            systemBus.call_noreply(method);
            doSettingsExist = true;
        }
        catch (const sdbusplus::exception::SdBusError& ex)
        {
            if (strcmp(ex.name(),
                       "xyz.openbmc_project.Common.Error.NotAllowed") != 0)
            {
                throw FwupdateError("Unable to import NVRAM: %s",
                                    ex.description());
            }
        }
        if (!doSettingsExist)
        {
            const fs::path dumpFile(tmpdir / nvramFile);
            const std::string cmd =
                strfmt("dd if=%s of=%s skip=%lu count=%lu", mtdDevice,
                       dumpFile.c_str(), nvramOffset / ddBlockSize,
                       nvramSize / ddBlockSize);
            const int rc = system(cmd.c_str());
            checkWaitStatus(rc, std::string());
            if (!fs::exists(dumpFile))
            {
                throw FwupdateError("Error reading NVRAM");
            }
            try
            {
                auto method =
                    systemBus.new_method_call(uefivarInterface, uefivarObject,
                                              uefivarInterface, "ImportVars");
                method.append(dumpFile.string());
                systemBus.call_noreply(method);
            }
            catch (const std::exception& ex)
            {
                throw FwupdateError("Unable to import NVRAM: %s", ex.what());
            }
        }
    }

    puts("Preserving 10GBE...");
    const fs::path dumpFile = tmpdir / gbeFile;
    const std::string cmd =
        strfmt("dd if=%s of=%s skip=%lu count=%lu", mtdDevice, dumpFile.c_str(),
               gbeOffset / ddBlockSize, gbeSize / ddBlockSize);
    const int rc = system(cmd.c_str());
    checkWaitStatus(rc, std::string());
    if (!fs::exists(dumpFile))
    {
        throw FwupdateError("Error reading 10GBE");
    }
}

bool BIOSUpdater::doAfterInstall(bool reset)
{
    if (writeGbeOnly)
    {
        return false;
    }

    // mtd-util doesn't work with symlinks
    const fs::path mtdDeviceReal = fs::canonical(
        fs::path(mtdDevice).parent_path() / fs::read_symlink(mtdDevice));

    puts("Restoring 10GBE...");
    const fs::path dumpFile = tmpdir / gbeFile;
    if (!fs::exists(dumpFile))
    {
        throw FwupdateError("Dump for 10GBE partition not found");
    }
    const std::string cmd =
        strfmt("mtd-util -d %s cp %s 0x%x", mtdDeviceReal.c_str(),
               dumpFile.c_str(), gbeOffset);
    const int rc = system(cmd.c_str());
    checkWaitStatus(rc, std::string());

    // reset BIOS version for bios_active ID
    updateDBusStoredVersion("/xyz/openbmc_project/software/bios_active", "N/A");

    if (reset)
    {
        puts("Reset UEFI settings...");
        try
        {
            auto method = systemBus.new_method_call(
                uefivarInterface, uefivarObject, uefivarInterface, "Reset");
            systemBus.call_noreply(method);
        }
        catch (const std::exception& ex)
        {
            throw FwupdateError("Error reseting UEFI settings: %s", ex.what());
        }
    }
    else
    {
        puts("Update UEFI settings...");
        const fs::path dumpFile(tmpdir / nvramFile);
        const std::string cmd = strfmt(
            "dd if=%s of=%s skip=%lu count=%lu", mtdDevice, dumpFile.c_str(),
            nvramOffset / ddBlockSize, nvramSize / ddBlockSize);
        const int rc = system(cmd.c_str());
        checkWaitStatus(rc, std::string());
        if (!fs::exists(dumpFile))
        {
            throw FwupdateError("Error reading NVRAM");
        }

        try
        {
            auto method =
                systemBus.new_method_call(uefivarInterface, uefivarObject,
                                          uefivarInterface, "UpdateVars");
            method.append(dumpFile.string());
            systemBus.call_noreply(method);
        }
        catch (const std::exception& ex)
        {
            throw FwupdateError("Error updating UEFI settings: %s", ex.what());
        }
    }

    return false; // reboot is not needed
}

bool BIOSUpdater::isFileFlashable(const fs::path& file) const
{
    return file.stem() == "vegman" &&
           (file.extension() == ".bin" || file.extension() == ".img");
}

void BIOSUpdater::readNvram(const std::string& file)
{
    BIOSUpdater upd(fs::temp_directory_path());
    upd.files.push_back("dummy");
    try
    {
        upd.lock();

        puts("Reading NVRAM...");
        const std::string cmd =
            strfmt("dd if=%s of=%s skip=%lu count=%lu", mtdDevice, file.c_str(),
                   nvramOffset / ddBlockSize, nvramSize / ddBlockSize);
        const int rc = system(cmd.c_str());
        checkWaitStatus(rc, std::string());

        upd.unlock();
    }
    catch (...)
    {
        try
        {
            upd.unlock();
        }
        catch (const std::exception& ex)
        {
            fprintf(stderr, "%s\n", ex.what());
        }
        throw;
    }
}

void BIOSUpdater::writeNvram(const std::string& file)
{
    BIOSUpdater upd(fs::temp_directory_path());
    upd.files.push_back("dummy");
    try
    {
        upd.lock();

        Tracer tracer("Writing NVRAM");
        const fs::path mtdDeviceReal = fs::canonical(
            fs::path(mtdDevice).parent_path() / fs::read_symlink(mtdDevice));
        const std::string cmd =
            strfmt("mtd-util -v -d %s cp %s 0x%x", mtdDeviceReal.c_str(),
                   file.c_str(), nvramOffset);
        const int rc = system(cmd.c_str());
        checkWaitStatus(rc, std::string());
        tracer.done();

        upd.unlock();
    }
    catch (...)
    {
        try
        {
            upd.unlock();
        }
        catch (const std::exception& ex)
        {
            fprintf(stderr, "%s\n", ex.what());
        }
        throw;
    }
}
