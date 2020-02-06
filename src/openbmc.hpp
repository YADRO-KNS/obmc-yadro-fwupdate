/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 YADRO.
 */

#include <filesystem>
#include <set>

namespace openbmc
{
namespace fs = std::filesystem;
using Files = std::set<fs::path>;

/**
 * @brief RAII wrapper for locking BMC reboot.
 */
struct Lock
{
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;

    Lock();
    ~Lock();
};

/**
 * @brief Clear RW partition.
 */
void reset(void);

/**
 * @brief Put firmware image to the ramfs where from it will be flashed.
 *
 * @param firmware  - Path to firmware image.
 * @param reset     - Flag to clean whitelist.
 */
void flash(const Files& firmware, bool reset = false);

/**
 * @brief Reboot the BMC.
 */
void reboot(bool interactive);

/**
 * @brief Get the set of required OpenBMC firmware files
 *
 * @param dir - Path to the directory where firmware package extracted
 *
 * @return Set of required files
 */
Files get_fw_files(const fs::path& dir);

} // namespace openbmc
