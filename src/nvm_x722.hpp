// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2021 YADRO

#include <array>
#include <filesystem>

/** @brief Read and write NVM for x722. */
class NvmX722
{
  public:
    using word_t = uint16_t;
    using mac_t = uint8_t[6]; // MAC address: 48 bits

    // Number of MAC addresses in the NVM image
    static constexpr size_t maxMac = 4;

    using MacAddresses = std::array<mac_t, maxMac>;

    // Offset of the 10GBE region inside the BIOS image
    static constexpr size_t nvmOffset = 0x00a36000;
    // Size of the 10GBE NVM image
    static constexpr size_t nvmSize = 0x005ba000;

    // Magic constants from the Intel's guide,
    // see "Intel® Ethernet Connection X722 - Programming MAC Addresses"
    static constexpr word_t controlWord = 0x0000;
    static constexpr word_t bankValid = 0x0249;
    static constexpr size_t bankSize = 0x10000;
    static constexpr word_t magicOffset1 = 0x0048;
    static constexpr word_t magicOffset2 = 0x0018;
    static constexpr word_t macHeaderSize = 0x0001;
    static constexpr word_t checksumOffset = 0x003f;
    static constexpr word_t checksumBase = 0xbaba;
    static constexpr word_t vpdOffset = 0x002f;
    static constexpr size_t vpdSize = 1024;
    static constexpr size_t pcieAltSize = 1024;

    /**
     * @brief Constructor: open file.
     *
     * @param[in] file Path to the file to load (10GBE or BIOS image)
     *
     * @throw std::exception in case of errors
     */
    NvmX722(const std::filesystem::path& file);

    /** @brief Destructor. */
    ~NvmX722();

    /**
     * @brief Get list of PF MAC addresses.
     *
     * @return list of MAC addresses
     *
     * @throw std::exception in case of errors
     */
    MacAddresses getMac() const;

    /**
     * @brief Set PF MAC addresses.
     *
     * @param[in] mac list of MAC addresses
     *
     * @throw std::exception in case of errors
     */
    void setMac(const MacAddresses& mac);

  private:
    /**
     * @brief Get data pointer for specified word offset.
     *
     * @param[in] offset word offset
     * @param[in] maxSize number of bytes that are needed at specified offset
     *
     * @return pointer to the data at specified word offset
     *
     * @throw std::exception in case of errors
     */
    uint8_t* wordPtr(word_t offset, size_t maxSize) const;

    /**
     * @brief Read word at specified word offset.
     *
     * @param[in] offset word offset
     *
     * @return word value
     */
    word_t readWord(word_t offset) const;

    /**
     * @brief Calculate checksum for current block.
     *
     * @return checksum
     */
    word_t calcChecksum() const;

  private:
    void* data;   ///< Pointer to the file data
    size_t size;  ///< Size of file data in bytes
    size_t start; ///< Offset to the valid block of NVM
};
