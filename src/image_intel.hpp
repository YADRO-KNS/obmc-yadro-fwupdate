/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 YADRO.
 */

#include "fwupdbase.hpp"

/**
 * @brief IntelPlatforms firmware updater.
 */
struct IntelPlatformsUpdater : public FwUpdBase
{
    using FwUpdBase::FwUpdBase;

    void reset() override;
    void lock() override;
    void doInstall(const fs::path& file) override;
    bool doAfterInstall(bool reset) override;
    bool isFileFlashable(const fs::path& file) const override;
};
