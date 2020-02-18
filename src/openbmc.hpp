/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 YADRO.
 */

#include "fwupdbase.hpp"

/**
 * @brief OpenBMC firmware updater.
 */
struct OpenBmcUpdater : public FwUpdBase
{
    using FwUpdBase::FwUpdBase;

    void lock() override;
    void unlock() override;
    void reset() override;
    void do_install(const fs::path& file) override;
    bool do_after_install(bool reset) override;
    bool is_file_belong(const fs::path& file) const override;

  private:
    bool locked = false;
};
