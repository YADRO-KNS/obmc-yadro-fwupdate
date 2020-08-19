/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 YADRO.
 */
#pragma once

#include <filesystem>
#include <string>

namespace fs = std::filesystem;

/**
 * @brief Firmware updater interface.
 */
struct FwUpdIFace
{
    virtual ~FwUpdIFace() = default;

    /**
     * @brief Enable firmware guard
     */
    virtual void lock()
    {
    }

    /**
     * @brief Disable firmware guard
     */
    virtual void unlock()
    {
    }

    /**
     * @brief Reset settings to manufacture default.
     */
    virtual void reset() = 0;

    /**
     * @brief Add firmware file
     *
     * @param file - path to the firmware file.
     * @return true if file successeful added
     */
    virtual bool add(const fs::path& file) = 0;

    /**
     * @brief Check signatures of firmware files.
     *
     * @param pulicKey - Path to public key file
     * @param hashFunc - Signature hash function
     */
    virtual void verify(const fs::path& pulicKey,
                        const std::string& hashFunc) = 0;

    /**
     * @brief Write firmware files to the flash drive
     *
     * @param reset - flag to reset stored settings.
     * @return True if reboot required
     */
    virtual bool install(bool reset) = 0;
};
