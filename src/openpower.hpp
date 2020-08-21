/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 YADRO.
 */

#pragma once

#include "fwupdbase.hpp"

/**
 * @brief OpenPOWER firmware updater.
 */
struct OpenPowerUpdater : public FwUpdBase
{
    using FwUpdBase::FwUpdBase;

    void lock() override;
    void unlock() override;
    void reset() override;
    void doBeforeInstall(bool reset) override;
    void doInstall(const fs::path& file) override;
    bool doAfterInstall(bool reset) override;
    bool isFileFlashable(const fs::path& file) const override;

  private:
    bool locked = false;
};
