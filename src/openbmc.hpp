/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 YADRO.
 */

#include "firmware.hpp"
#include "purpose.hpp"

namespace openbmc
{

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
 * @brief Reboot the BMC.
 */
void reboot(bool interactive);

using namespace firmware;

struct OpenBmcUpdater : public UpdaterBase
{
    using UpdaterBase::UpdaterBase;

    void do_install(const fs::path& file) override;
    void do_after_install(bool reset) override;

    bool check_purpose(const std::string& purpose) const override
    {
        return purpose == purpose::System || purpose == purpose::BMC;
    }
};

} // namespace openbmc
