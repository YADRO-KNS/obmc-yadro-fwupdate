/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 YADRO.
 */

#pragma once

#include "strfmt.hpp"

#include <stdexcept>

class FwupdateError : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;

    template <typename... Args>
    FwupdateError(const char* fmt, Args&&... args) :
        std::runtime_error::runtime_error(
            strfmt(fmt, std::forward<Args>(args)...))
    {
    }
};
