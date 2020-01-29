/**
 * @brief OpenPOWER firmware tools declarations.
 *
 * This file is part of OpenBMC/OpenPOWER firmware updater.
 *
 * Copyright 2020 YADRO
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
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
