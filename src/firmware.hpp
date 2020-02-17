/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 YADRO.
 */
#pragma once

#include <filesystem>
#include <vector>

namespace firmware
{
namespace fs = std::filesystem;

struct UpdaterIFace
{
    virtual ~UpdaterIFace() = default; // Suppress syntastic/clang warning

    /**
     * @brief Enable firmware guard
     */
    virtual void lock(void) = 0;

    /**
     * @brief Disable firmware guard
     */
    virtual void unlock(void) = 0;

    /**
     * @brief Reset settings to manufacture default.
     */
    virtual void reset(void) = 0;

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

using Files = std::vector<fs::path>;

struct UpdaterBase : public UpdaterIFace
{
    /**
     * @brief UpdaterBase object constructor
     *
     * @param tmpdir - path to temporary directory
     */
    UpdaterBase(const fs::path& tmpdir);

    bool add(const fs::path& file) override;
    void verify(const fs::path& publicKey,
                const std::string& hashFunc) override;
    bool install(bool reset) override;

  protected:
    /**
     * @brief Check if specified file belongs to this firmware type.
     */
    virtual bool is_file_belongs(const fs::path& file) const = 0;

    /**
     * @brief Will be called before installation procedure
     *
     * @param reset - flag to reset stored settings
     */
    virtual void do_before_install(bool /*reset*/)
    {
    }

    /**
     * @brief Write specified file to the flash drive
     *
     * @param file - path to firmware file
     */
    virtual void do_install(const fs::path& file) = 0;

    /**
     * @brief Will be called after success installation
     *
     * @param reset - flag to reset stored settings
     * @return True if reboot required
     */
    virtual bool do_after_install(bool /*reset*/) = 0;

    Files files;     //! List of firmware files
    fs::path tmpdir; //! Temporary directory
};

} // namespace firmware
