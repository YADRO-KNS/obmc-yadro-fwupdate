/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 YADRO.
 */
#include "config.h"

#include "factory.hpp"

#include <regex>

namespace factory
{

static std::vector<std::pair<std::regex, UpdaterCreator>> factory;

void register_updater(const char* regex, UpdaterCreator creator)
{
    factory.emplace_back(std::regex(regex), creator);
}

static void append_file(firmware::Files& files, const std::regex& regex,
                        const fs::path& file)
{
    if (fs::is_regular_file(file) && file.extension() != SIGNATURE_FILE_EXT &&
        std::regex_match(file.filename().string(), regex))
    {
        files.emplace_back(file);
    }
}

UpdatersList create_updaters(const fs::path& path, const fs::path& tmpdir)
{
    UpdatersList instances;

    for (const auto& f : factory)
    {
        firmware::Files files;

        append_file(files, f.first, path);
        if (fs::is_directory(path))
        {
            for (const auto it : fs::directory_iterator(path))
            {
                append_file(files, f.first, it.path());
            }
        }

        if (!files.empty())
        {
            instances.emplace_back(f.second(files, tmpdir));
        }
    }

    return instances;
}

} // namespace factory
