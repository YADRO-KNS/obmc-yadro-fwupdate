/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2021 YADRO.
 */

#include "config.h"

#include "image_bios.hpp"

#include "dbus.hpp"
#include "fwupderr.hpp"
#ifdef INTEL_X722_SUPPORT
#include "nvm_x722.hpp"
#endif // INTEL_X722_SUPPORT
#include "subprocess.hpp"
#include "tracer.hpp"

#ifdef USE_PCA9698_OEPOL
extern "C"
{
#include <i2c/smbus.h>
}
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif // USE_PCA9698_OEPOL

#include <chrono>
#include <fstream>
#include <thread>

static const fs::path aspeedSMC = "/sys/bus/platform/drivers/aspeed-smc";
static constexpr auto spiDriver = "1e631000.spi";
static const char* mtdDevice = "/dev/mtd/bios";
static constexpr auto gpioNameBIOSSel = "MIO_BIOS_SEL";

#ifdef GOLDEN_FLASH_SUPPORT
extern bool useGoldenFlash; // Defined in main.cpp
static constexpr auto gpioNameActiveFlashSel = "SPI_BIOS_ACTIVE_FLASH_SEL";
static constexpr auto mainFlashChip = 0;
static constexpr auto goldenFlashChip = 1;
#endif // GOLDEN_FLASH_SUPPORT

static constexpr auto gpioOwner = "fwupdate";
#ifdef USE_PCA9698_OEPOL
static constexpr auto pca9698Bus = "/dev/i2c-11";
static constexpr auto pca9698Addr = 0x27;
static constexpr auto pca9698InputReg = 0x00;
static constexpr auto pca9698ModeReg = 0x2a;
static constexpr auto pca9698OEPolBit = 0x01;
#endif // USE_PCA9698_OEPOL
static constexpr size_t nvramOffset = 0x01000000;
static constexpr size_t nvramSize = 0x00080000;
static const char* nvramFile = "nvram.bin";
static constexpr size_t ddBlockSize = 512;

#ifdef INTEL_X722_SUPPORT
static const char* gbeFile = "gbe.bin";
bool BIOSUpdater::writeGbeOnly = false;
#endif // INTEL_X722_SUPPORT

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

#ifdef USE_PCA9698_OEPOL
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
#endif // USE_PCA9698_OEPOL

#ifdef INTEL_X722_SUPPORT
/**
 * @brief Dump GBE blob
 *
 * @param source[in]      bundle filename or mtd device
 * @param destination[in] filename to write the dump
 *
 * @throw FwupdateError in case of errors
 */
static void dumpGbe(const fs::path& source, const fs::path& destination)
{
    puts("Preserving 10GBE...");
    std::string cmd =
        strfmt("dd if=%s of=%s skip=%lu count=%lu", source.c_str(),
               destination.c_str(), NvmX722::nvmOffset / ddBlockSize,
               NvmX722::nvmSize / ddBlockSize);
    int rc = system(cmd.c_str());
    checkWaitStatus(rc, std::string());
    if (!fs::exists(destination))
    {
        throw FwupdateError("Error reading 10GBE");
    }
}

/**
 * @brief Write GBE blob to the flash drive
 *
 * @param img[in]    GBE blob image
 * @param device[in] mtd device
 *
 * @throw FwupdateError in case of errors
 */
static void flashGbe(const fs::path& image, const fs::path& device)
{
    // mtd-util doesn't work with symlinks
    const auto mtdDeviceReal = fs::canonical(device);
    puts("Writing GBE...");
    auto cmd = strfmt("mtd-util -d %s cp %s 0x%x", mtdDeviceReal.c_str(),
                      image.c_str(), NvmX722::nvmOffset);
    auto rc = system(cmd.c_str());
    checkWaitStatus(rc, std::string());
}

inline bool empty(const NvmX722::mac_t& mac)
{
    bool ret = true;
    const size_t len = sizeof(mac) / sizeof(*mac);
    for (size_t i = 0; i < len && ret; ++i)
    {
        ret &= (mac[i] == 0x00);
    }
    return ret;
}

inline size_t count(const NvmX722::MacAddresses& addrs)
{
    size_t ret = 0;
    for (const auto& mac : addrs)
    {
        if (!empty(mac))
        {
            ++ret;
        }
    }
    return ret;
}
#endif // INTEL_X722_SUPPORT

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
            setGPIOOutput(PCH_POWER_PIN, PCH_POWER_DOWN_VALUE, gpioPCHPower);

#ifdef USE_PCA9698_OEPOL
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
#endif // USE_PCA9698_OEPOL

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
#ifdef GOLDEN_FLASH_SUPPORT
            // Force using the main flash drive
            setGPIOOutput(gpioNameActiveFlashSel, mainFlashChip,
                          gpioActiveFlashSel);
#endif // GOLDEN_FLASH_SUPPORT
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
#ifdef GOLDEN_FLASH_SUPPORT
            releaseGPIO(gpioActiveFlashSel);
#endif // GOLDEN_FLASH_SUPPORT
            releaseGPIO(gpioBIOSSel);
            tracer.done();
        }
        {
            Tracer tracer("Restoring PCH power");
            releaseGPIO(gpioPCHPower);

#ifdef USE_PCA9698_OEPOL
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
#endif // USE_PCA9698_OEPOL

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
#ifdef INTEL_X722_SUPPORT
    if (writeGbeOnly)
    {
        const fs::path partFile(tmpdir / gbeFile);
        dumpGbe(file, partFile);
        flashGbe(partFile, mtdDevice);
    }
    else
    {
        // modify BIOS image to preserve x722 MAC addresses
        try
        {
            Tracer tracer("Preserving x722 MAC addresses");
            auto mac = NvmX722(tmpdir / gbeFile).getMac();
            NvmX722(file).setMac(mac);
            tracer.done();
        }
        catch (const std::exception& ex)
        {
            throw FwupdateError("Unable to preserve x722 MAC: %s", ex.what());
        }
#endif // INTEL_X722_SUPPORT

#ifdef GOLDEN_FLASH_SUPPORT
        if (useGoldenFlash)
        {
            Tracer tracer("Switching to golden flash");

            // This makes the BMC to reinit the driver which might be attached
            // with wrong data on the hosts equipped with em100.
            unbindSPIDriver();

            // Swap ChipSelect of main and golden flash drives
            gpioActiveFlashSel.set_value(goldenFlashChip);

            // Reinit MTD deivce
            bindSPIDriver();
            tracer.done();
        }
#endif // GOLDEN_FLASH_SUPPORT

        printf("Writing %s to %s\n", file.filename().c_str(), mtdDevice);
        int rc = system(
            strfmt(FLASHCP_CMD " -v %s %s", file.c_str(), mtdDevice).c_str());
        checkWaitStatus(rc, "");
#ifdef INTEL_X722_SUPPORT
    }
#endif // INTEL_X722_SUPPORT
}

void BIOSUpdater::doBeforeInstall(bool reset)
{
#ifdef INTEL_X722_SUPPORT
    if (writeGbeOnly)
    {
        return;
    }
#endif // INTEL_X722_SUPPORT

    if (!reset)
    {
        puts("Preserving NVRAM...");
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
    }

#ifdef INTEL_X722_SUPPORT
    dumpGbe(mtdDevice, tmpdir / gbeFile);
#endif // INTEL_X722_SUPPORT
}

bool BIOSUpdater::doAfterInstall(bool reset)
{
#ifdef INTEL_X722_SUPPORT
    if (writeGbeOnly)
    {
        return false;
    }
#endif // INTEL_X722_SUPPORT

    // mtd-util doesn't work with symlinks
    const fs::path mtdDeviceReal = fs::canonical(
        fs::path(mtdDevice).parent_path() / fs::read_symlink(mtdDevice));

    if (!reset)
    {
        puts("Restoring NVRAM...");
        const fs::path dumpFile(tmpdir / nvramFile);
        if (!fs::exists(dumpFile))
        {
            throw FwupdateError("Dump for NVRAM partition not found");
        }
        const std::string cmd =
            strfmt("mtd-util -d %s cp %s 0x%x", mtdDeviceReal.c_str(),
                   dumpFile.c_str(), nvramOffset);
        const int rc = system(cmd.c_str());
        checkWaitStatus(rc, std::string());
    }

    // reset BIOS version for bios_active ID
    updateDBusStoredVersion("/xyz/openbmc_project/software/bios_active", "N/A");

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

void BIOSUpdater::resetHostMacAddrs()
{
    BIOSUpdater upd(fs::temp_directory_path());
    upd.files.push_back("dummy");

    try
    {

        {
            static constexpr auto resetMacPath =
                "/xyz/openbmc_project/control/host0/boot/one_time";
            static constexpr auto resetMacIface =
                "xyz.openbmc_project.Control.Boot.ResetMAC";

            Tracer tracer("Set ResetMAC flag");

            auto objects = getObjects(resetMacPath, {resetMacIface});
            if (!objects.empty())
            {
                setProperty(objects.begin()->first, resetMacPath, resetMacIface,
                            "ResetMAC", true);
            }
            else
            {
                tracer.fail();
                printf("WARNING: No service providing `ResetMAC` property "
                       "found!\n");
            }

            tracer.done();
        }

#ifdef INTEL_X722_SUPPORT
        auto fruMacAddrs = NvmX722::getMacFromFRU();
        if (count(fruMacAddrs) > 0)
        {
            upd.lock();

            const fs::path dumpFile = upd.tmpdir / gbeFile;
            dumpGbe(mtdDevice, dumpFile);

            {
                Tracer tracer("Preserving x722 MAC addresses");
                NvmX722 gbe(dumpFile);

                auto macAddrs = gbe.getMac();
                for (size_t i = 0; i < fruMacAddrs.size(); ++i)
                {
                    if (!empty(fruMacAddrs[i]))
                    {
                        std::swap(macAddrs[i], fruMacAddrs[i]);
                    }
                }
                gbe.setMac(macAddrs);
                tracer.done();
            }

            flashGbe(dumpFile, mtdDevice);

            upd.unlock();
        }
        else
        {
            printf("WARNING: No x722 MAC addresses found in FRU!\n");
        }
#endif // INTEL_X722_SUPPORT
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
