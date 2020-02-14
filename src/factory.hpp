/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 YADRO.
 */
#pragma once

#include "firmware.hpp"

#include <memory>

namespace factory
{
namespace fs = firmware::fs;

using UpdaterPtr = std::unique_ptr<firmware::UpdaterIFace>;
using UpdaterCreator = UpdaterPtr (*)(firmware::Files&, const fs::path&);
using UpdatersList = std::vector<UpdaterPtr>;

/**
 * @brief Make new firmware updater object
 *
 * @tparam TUpdater - Class of updater implementation
 * @param files     - List of firmware files
 * @param tmpdir    - Path to the temporary directory
 *
 * @return Pointer to updater interface
 */
template <typename TUpdater>
UpdaterPtr make_updater(firmware::Files& files, const fs::path& tmpdir)
{
    static_assert(std::is_base_of<firmware::UpdaterBase, TUpdater>::value,
                  "TUpdater must derive from firmware::UpdaterBase");
    return std::make_unique<TUpdater>(files, tmpdir);
}

/**
 * @brief Register updater interface in the factory
 *
 * @param regex   - Regex mask for filenames
 * @param creator - Function to create firmware updater interface
 */
void register_updater(const char* regex, UpdaterCreator creator);

/**
 * @brief Create list of firmware updaters for specified path
 *
 * @param path   - Path to the single firmware file or directory
 * @param tmpdir - Path to temporary directory
 *
 * @return List of firmware updater interfaces
 */
UpdatersList create_updaters(const fs::path& path, const fs::path& tmpdir);

} // namespace factory
