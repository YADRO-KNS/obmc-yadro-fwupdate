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
        // Not supported yet.
    }
    void lock() override;
    void unlock() override;
    void doInstall(const fs::path& file) override;
    void doBeforeInstall(bool reset) override;
    bool doAfterInstall(bool reset) override;
    bool isFileFlashable(const fs::path& file) const override;

  private:
    bool locked = false;
    gpiod::line gpioPCHPower;
    gpiod::line gpioBIOSSel;
    int pca9698FD = -1;
};
