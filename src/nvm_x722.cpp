// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2021 YADRO

#include "config.h"

#include "nvm_x722.hpp"

#include "dbus.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <system_error>

NvmX722::NvmX722(const std::filesystem::path& file)
{
    int fd = -1;
    data = MAP_FAILED;

    try
    {
        // open file
        fd = open(file.c_str(), O_RDWR);
        if (fd == -1)
        {
            throw std::system_error(errno, std::generic_category());
        }

        // get file size
        struct stat st;
        if (fstat(fd, &st) == -1)
        {
            throw std::system_error(errno, std::generic_category());
        }
        size = st.st_size;

        // map file to memory
        data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (data == MAP_FAILED)
        {
            throw std::system_error(errno, std::generic_category());
        }

        // check min size
        if (size < nvmSize)
        {
            throw std::runtime_error("The image file is too small to contain a 10GBE region");
        }

        // move start offset if input file is a BIOS image
        start = (size == nvmSize) ? 0 : nvmOffset;

        // search for valid bank
        if (readWord(controlWord) != bankValid)
        {
            start += bankSize;
            if (readWord(controlWord) != bankValid)
            {
                throw std::runtime_error("No valid bank in 10GBE");
            }
        }
    }
    catch (...)
    {
        if (fd != -1)
        {
            close(fd);
        }
        if (data != MAP_FAILED)
        {
            munmap(data, size);
        }
        throw;
    }

    close(fd);
}

NvmX722::~NvmX722()
{
    munmap(data, size);
}

NvmX722::MacAddresses NvmX722::getMac() const
{
    MacAddresses mac;

    // get offset for MAC settings
    word_t offset = readWord(magicOffset1);
    offset += magicOffset2;
    offset += readWord(offset);

    // read PF MAC addresses
    for (size_t i = 0; i < mac.size(); ++i)
    {
        offset += macHeaderSize;
        const uint8_t* macPtr = wordPtr(offset, sizeof(mac_t));
        std::copy(macPtr, macPtr + sizeof(mac_t), mac[i]);
        offset += sizeof(mac_t) / sizeof(word_t);
    }

    return mac;
}

void NvmX722::setMac(const MacAddresses& mac)
{
    // get offset for MAC settings
    word_t offset = readWord(magicOffset1);
    offset += magicOffset2;
    offset += readWord(offset);

    // write PF MAC addresses
    for (size_t i = 0; i < mac.size(); ++i)
    {
        offset += macHeaderSize;
        uint8_t* macPtr = wordPtr(offset, sizeof(mac_t));
        std::copy(mac[i], mac[i] + sizeof(mac_t), macPtr);
        offset += sizeof(mac_t) / sizeof(word_t);
    }

    // update checksum
    uint8_t* chkSumPtr = wordPtr(checksumOffset, sizeof(word_t));
    *reinterpret_cast<word_t*>(chkSumPtr) = calcChecksum();

    // write changes to persistent storage
    if (msync(data, size, MS_SYNC) == -1)
    {
        throw std::system_error(errno, std::generic_category());
    }
}

uint8_t* NvmX722::wordPtr(word_t offset, size_t maxSize) const
{
    const size_t byteOffset = start + offset * sizeof(word_t);
    if (byteOffset + maxSize > size)
    {
        throw std::runtime_error("Invalid 10GBE NVM");
    }
    return reinterpret_cast<uint8_t*>(data) + byteOffset;
}

NvmX722::word_t NvmX722::readWord(word_t offset) const
{
    const uint8_t* ptr = wordPtr(offset, sizeof(word_t));
    return *reinterpret_cast<const word_t*>(ptr);
}

NvmX722::word_t NvmX722::calcChecksum() const
{
    word_t chkSum = 0;
    const word_t vpdStart = readWord(vpdOffset);

    for (word_t i = 0; i < bankSize / sizeof(word_t); ++i)
    {
        if (i == checksumOffset)
        {
            continue; // skip checksum word
        }
        if (i >= vpdStart && i < vpdStart + vpdSize / sizeof(word_t))
        {
            continue; // skip VPD module
        }
        if (i >= (bankSize - pcieAltSize) / sizeof(word_t))
        {
            break; // skip PCIe ALT module (it resides at the end of bank)
        }
        chkSum += readWord(i);
    }

    return checksumBase - chkSum;
}

/**
 * @brief Convert hex character to binary value
 */
inline uint8_t charToByte(const char c)
{
    if (c >= '0' && c <= '9')
    {
        return static_cast<uint8_t>(c - '0');
    }
    else if (c >= 'a' && c <= 'f')
    {
        return static_cast<uint8_t>(10 + c - 'a');
    }
    else if (c >= 'A' && c <= 'F')
    {
        return static_cast<uint8_t>(10 + c - 'A');
    }

    throw std::invalid_argument("Illegal character");
}

struct BoardInfo
{
    static constexpr uint8_t macAddressType = 0x01;
    static constexpr uint8_t x722MacAddr = 0x03;

    uint8_t recordType = 0;
    uint8_t addrType = 0;
    NvmX722::mac_t mac = {0};

    /**
     * @brief Get record designation.
     */
    inline uint8_t designation() const
    {
        return static_cast<uint8_t>((addrType & 0xF0) >> 4);
    }

    /**
     * @brief MAC address index
     */
    inline uint8_t index() const
    {
        return static_cast<uint8_t>(addrType & 0x0F);
    }

    /**
     * @brief Parse hex string into BoardInfo structure
     *
     * @param s - Hex string
     *
     * @return BoardInfo structure
     *
     * @throw std::exception in case of errors
     */
    static BoardInfo fromString(const std::string& s)
    {
        BoardInfo bi;
        uint8_t* bytes = reinterpret_cast<uint8_t*>(&bi);
        const size_t len = std::min(s.length(), sizeof(bi) * 2);
        for (size_t i = 0; i < len; ++i)
        {
            size_t idx = i / 2;
            if ((i % 2) == 0)
            {
                bytes[idx] = charToByte(s[i]) << 4;
            }
            else
            {
                bytes[idx] |= charToByte(s[i]);
            }
        }

        return bi;
    }
} __attribute__((packed));

using Property = std::variant<std::string>;
using Properties = std::map<PropertyName, Property>;
/**
 * @brief Get all D-Bus properties from the specified service.
 *
 * NOTE: We don't put this method into the generic dbus.hpp header because as of
 *       now it only supports `string` properties and is not fully usable as a
 *       generic method
 *
 * @param busname - D-Bus service name
 * @param path    - Object path
 * @param iface   - Poperties interface
 *
 * @return Map of propety' names and values.
 *
 * @throw FwupdateError if case of error
 */
Properties getAllProperties(const BusName& busname, const Path& path,
                            const Interface& iface)
{
    auto req = systemBus.new_method_call(
        busname.c_str(), path.c_str(), SYSTEMD_PROPERTIES_INTERFACE, "GetAll");
    req.append(iface);
    Properties props;

    try
    {
        systemBus.call(req).read(props);
    }
    catch (const sdbusplus::exception::SdBusError& e)
    {
        throw FwupdateError("Get all properties call failed: %s", e.what());
    }

    return props;
}

NvmX722::MacAddresses NvmX722::getMacFromFRU()
{
    static const std::string mboard{"Motherboard"};
    static const std::string boardInfoAm{"BOARD_INFO_AM"};
    static const char* fruDevicePath = "/xyz/openbmc_project/FruDevice";
    static const char* fruDeviceIface = "xyz.openbmc_project.FruDevice";

    auto objects = getSubTree(fruDevicePath, {fruDeviceIface});
    for (const auto& [path, services] : objects)
    {
        if (!std::equal(mboard.crbegin(), mboard.crend(), path.crbegin()))
        {
            continue;
        }

        for (const auto& [service, _] : services)
        {
            auto properties = getAllProperties(service, path, fruDeviceIface);
            MacAddresses mac{0};
            for (const auto& [name, value] : properties)
            {
                if (!std::equal(boardInfoAm.cbegin(), boardInfoAm.cend(),
                                name.cbegin()))
                {
                    continue;
                }

                auto bi = BoardInfo::fromString(std::get<std::string>(value));
                if (bi.recordType == BoardInfo::macAddressType &&
                    bi.designation() == BoardInfo::x722MacAddr)
                {
                    if (bi.index() >= mac.size())
                    {
                        throw FwupdateError("Invalid MAC index in FRU");
                    }
                    std::swap(mac[bi.index()], bi.mac);
                }
            }

            return mac;
        }
    }

    throw FwupdateError("No properly records found in FRU");
}
