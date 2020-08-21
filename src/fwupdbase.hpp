/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 YADRO.
 */
#pragma once

#include "fwupdiface.hpp"

#include <vector>

using Files = std::vector<fs::path>;

/**
 * @brief Base Firmware updater impementation.
 */
struct FwUpdBase : public FwUpdIFace
{
    /**
     * @brief FwUpdBase object constructor
     *
     * @param tmpdir - path to temporary directory
     */
    FwUpdBase(const fs::path& tmpdir);

    bool add(const fs::path& file) override;
    void verify(const fs::path& publicKey,
                const std::string& hashFunc) override;
    bool install(bool reset) override;

  protected:
    /**
     * @brief Check if specified file can be flashed by this updater instance.
     */
    virtual bool isFileFlashable(const fs::path& file) const = 0;

    /**
     * @brief Will be called before installation procedure
     *
     * @param reset - flag to reset stored settings
     */
    virtual void doBeforeInstall(bool /*reset*/)
    {
    }

    /**
     * @brief Write specified file to the flash drive
     *
     * @param file - path to firmware file
     */
    virtual void doInstall(const fs::path& file) = 0;

    /**
     * @brief Will be called after success installation
     *
     * @param reset - flag to reset stored settings
     * @return True if reboot required
     */
    virtual bool doAfterInstall(bool /*reset*/) = 0;

    Files files;     //! List of firmware files
    fs::path tmpdir; //! Temporary directory
};
