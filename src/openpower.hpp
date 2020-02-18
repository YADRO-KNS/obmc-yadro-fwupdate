/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 YADRO.
 */

#pragma once

#include "fwupdbase.hpp"

struct OpenPowerUpdater : public FwUpdBase
{
    using FwUpdBase::FwUpdBase;

    void lock(void) override;
    void unlock(void) override;
    void reset(void) override;
    void do_before_install(bool reset) override;
    void do_install(const fs::path& file) override;
    bool do_after_install(bool reset) override;
    bool is_file_belongs(const fs::path& file) const override;

  private:
    bool locked = false;
};
