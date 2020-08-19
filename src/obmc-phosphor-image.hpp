/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 YADRO.
 */

#include "fwupdbase.hpp"

/**
 * @brief OBMCPhosphorImage firmware updater.
 */
struct OBMCPhosphorImageUpdater : public FwUpdBase
{
    using FwUpdBase::FwUpdBase;

    void reset() override;
    void do_install(const fs::path& file) override;
    bool do_after_install(bool reset) override;
    bool is_file_belong(const fs::path& file) const override;
};
