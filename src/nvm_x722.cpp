// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2021 YADRO

#include "nvm_x722.hpp"

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
