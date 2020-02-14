/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 YADRO.
 */

#include "firmware.hpp"

#include "signature.hpp"

namespace firmware
{

UpdaterBase::UpdaterBase(Files& files, const fs::path& tmpdir) :
    files(std::move(files)), tmpdir(tmpdir)
{
}

void UpdaterBase::verify(const fs::path& publicKey, const std::string& hashFunc)
{
    for (const auto& file : files)
    {
        verify_file(publicKey, hashFunc, file);
    }
}

void UpdaterBase::install(bool reset)
{
    do_before_install(reset);

    for (const auto& file : files)
    {
        do_install(file);
    }

    do_after_install(reset);
}

} // namespace firmware
