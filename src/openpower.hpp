/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 YADRO.
 */

#pragma once

#include <filesystem>
#include <set>

namespace openpower
{
namespace fs = std::filesystem;
using Files = std::set<fs::path>;

/**
 * @brief Lock access to PNOR flash drive.
 */
void lock(void);

/**
 * @brief Unlock access to PNOR flash drive.
 */
void unlock(void);

/**
 * @brief Clear PNOR partitions on the flash device.
 */
void reset(void);

/**
 * @brief Flash firmware to flash drive.
 *        Optionally keep NVRAM.
 *
 * @param firmware  - Set of firmware images.
 * @param tmpdir    - Path to temporary directory.
 *                    If not specified the NVRAM will not be restored.
 */
void flash(const Files& firmware, const fs::path& tmpdir = "");

/**
 * @brief Get the set of required OpenPOWER firmware files
 *
 * @param dir - Path to the directory where firmware package extracted
 *
 * @return Set of required files
 */
Files get_fw_files(const fs::path& dir);

} // namespace openpower
