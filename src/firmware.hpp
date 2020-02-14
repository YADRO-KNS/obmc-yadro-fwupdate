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
     */
    virtual void install(bool reset) = 0;

    /**
     * @brief Check if the updater corresponds to the specified purpose.
     */
    virtual bool check_purpose(const std::string& purpose) const = 0;
};

using Files = std::vector<fs::path>;

struct UpdaterBase : public UpdaterIFace
{
    /**
     * @brief UpdaterBase object constructor
     *
     * @param files  - list of firmware files
     * @param tmpdir - path to temporary directory
     */
    UpdaterBase(Files& files, const fs::path& tmpdir);

    void verify(const fs::path& publicKey,
                const std::string& hashFunc) override;
    void install(bool reset) override;

  protected:
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
     */
    virtual void do_after_install(bool /*reset*/)
    {
    }

    Files files;     //! List of firmware files
    fs::path tmpdir; //! Temporary directory
};

} // namespace firmware
