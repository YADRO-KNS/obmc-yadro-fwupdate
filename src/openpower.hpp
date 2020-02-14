/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 YADRO.
 */

#pragma once

#include "firmware.hpp"
#include "purpose.hpp"

namespace openpower
{
/**
 * @brief RAII wrapper for locking access to PNOR flash drive.
 */
struct Lock
{
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;

    Lock();
    ~Lock();
};

/**
 * @brief Clear PNOR partitions on the flash device.
 */
void reset(void);

using namespace firmware;

struct OpenPowerUpdater : public UpdaterBase
{
    using UpdaterBase::UpdaterBase;

    void do_before_install(bool reset) override;
    void do_install(const fs::path& file) override;
    void do_after_install(bool reset) override;

    bool check_purpose(const std::string& purpose) const override
    {
        return purpose == purpose::System || purpose == purpose::Host;
    }
};

} // namespace openpower
