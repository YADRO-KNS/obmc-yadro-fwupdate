/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 YADRO.
 */

#pragma once

#include "firmware.hpp"

#include <memory>
#include <vector>

namespace firmware
{

struct FwUpdate
{
    FwUpdate(const FwUpdate&) = delete;
    FwUpdate& operator=(const FwUpdate&) = delete;

    FwUpdate();
    ~FwUpdate();

    /**
     * @brief Enable guards for all firmware types
     */
    void lock(void);

    /**
     * @brief Disable guards for all firmware types
     */
    void unlock(void);

    /**
     * @brief Reset all settings to manufacture default.
     */
    void reset(void);

    /**
     * @brief Unpack bundle package.
     *
     * @param package - path to the firmware package
     */
    void unpack(const fs::path& package);

    /**
     * @brief Verify signature of firmware package
     */
    void verify(void);

    /**
     * @brief Install firmware
     *
     * @param reset - flag to skip restore of the settings
     * @return True if reboot required
     */
    bool install(bool reset);

  protected:
    /**
     * @brief Add specified file to updater implementations
     *
     * @param file - path to firmware file
     *
     * @return True if file successful added
     */
    bool add_file(const fs::path& file);

    /**
     * @brief Create fs::path object and check existense
     */
    fs::path get_fw_file(const std::string& filename);

    /**
     * @brief Verify the MANIFEST and publickey file using available public keys
     *        and hash on the system.
     */
    void system_level_verify(void);

    /**
     * @brief Compare system and package machine types.
     */
    void check_machine_type(void);

  private:
    fs::path tmpdir;
    std::vector<std::unique_ptr<UpdaterIFace>> updaters;
};

} // namespace firmware
