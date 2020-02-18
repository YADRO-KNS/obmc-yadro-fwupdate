/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 YADRO.
 */

#include "fwupdbase.hpp"

#include "signature.hpp"
#include "tracer.hpp"

FwUpdBase::FwUpdBase(const fs::path& tmpdir) : tmpdir(tmpdir)
{
}

bool FwUpdBase::add(const fs::path& file)
{
    bool ret = fs::is_regular_file(file) && is_file_belong(file);
    if (ret)
    {
        files.emplace_back(file);
    }
    return ret;
}

void FwUpdBase::verify(const fs::path& publicKey, const std::string& hashFunc)
{
    for (const auto& file : files)
    {
        Tracer tracer("Check signature for %s", file.filename().c_str());
        verify_file(publicKey, hashFunc, file);
        tracer.done();
    }
}

bool FwUpdBase::install(bool reset)
{
    do_before_install(reset);

    for (const auto& file : files)
    {
        do_install(file);
    }

    return do_after_install(reset);
}
