/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2021 YADRO.
 */

#include "fwupdbase.hpp"

#include <gpiod.hpp>

/**
 * @brief Vegman's BIOS firmware updater.
 */
struct BIOSUpdater : public FwUpdBase
{
    using FwUpdBase::FwUpdBase;

    void reset() override
    {
        // There is nothing to reset in the BIOS.
    }
    void lock() override;
    void unlock() override;
    void doInstall(const fs::path& file) override;
    bool doAfterInstall(bool) override
    {
        // The BIOS update does not require the BMC reboot.
        return false;
    }
    bool isFileFlashable(const fs::path& file) const override;

  private:
    bool locked = false;
    gpiod::line gpioPCHPower;
    gpiod::line gpioBIOSSel;
};
