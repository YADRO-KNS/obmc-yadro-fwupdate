/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 YADRO.
 */

#pragma once

#include "fwupdiface.hpp"

#include <memory>
#include <vector>

/**
 * @brief General firmware updater implementation.
 */
struct FwUpdate
{
    FwUpdate(const FwUpdate&) = delete;
    FwUpdate& operator=(const FwUpdate&) = delete;

    /**
     * @brief FwUpdate object constructor
     *
     * @param force - flag to skip locking
     */
    FwUpdate(bool force);

    ~FwUpdate();

    /**
     * @brief Reset all settings to manufacture default.
     */
    void reset();

    /**
     * @brief Unpack bundle package.
     *
     * @param package - path to the firmware package
     */
    void unpack(const fs::path& package);

    /**
     * @brief Verify signature of firmware package
     */
    void verify();

    /**
     * @brief Install firmware
     *
     * @param reset - flag to skip restore of the settings
     * @return True if reboot required
     */
    bool install(bool reset);

  protected:
    /**
     * @brief Enable guards for all firmware types
     */
    void lock();

    /**
     * @brief Disable guards for all firmware types
     */
    void unlock();

    /**
     * @brief Add specified file to updater implementations
     *
     * @param file - path to firmware file
     *
     * @return True if file successful added
     */
    bool addFile(const fs::path& file);

    /**
     * @brief Create fs::path object and check existence
     */
    fs::path getFWFile(const std::string& filename);

    /**
     * @brief Verify the MANIFEST and publickey file using available public keys
     *        and hash on the system.
     */
    void systemLevelVerify();

    /**
     * @brief Compare system and package machine types.
     */
    void checkMachineType();

  private:
    fs::path tmpdir;
    bool force;
    std::vector<std::unique_ptr<FwUpdIFace>> updaters;
    bool locked = false;
};
