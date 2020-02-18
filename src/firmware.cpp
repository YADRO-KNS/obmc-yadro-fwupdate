/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 YADRO.
 */

#include "firmware.hpp"
#include "signature.hpp"

UpdaterBase::UpdaterBase(const fs::path& tmpdir) : tmpdir(tmpdir)
{
}

bool UpdaterBase::add(const fs::path& file)
{
    bool ret = fs::is_regular_file(file) && is_file_belongs(file);
    if (ret)
    {
        files.emplace_back(file);
    }
    return ret;
}

void UpdaterBase::verify(const fs::path& publicKey, const std::string& hashFunc)
{
    for (const auto& file : files)
    {
        verify_file(publicKey, hashFunc, file);
    }
}

bool UpdaterBase::install(bool reset)
{
    do_before_install(reset);

    for (const auto& file : files)
    {
        do_install(file);
    }

    return do_after_install(reset);
}
