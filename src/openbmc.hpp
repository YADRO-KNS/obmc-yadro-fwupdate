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
 * @brief Enable BMC reboot guard.
 */
void lock(void);

/**
 * @brief Disable BMC reboot guard.
 */
void unlock(void);

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
