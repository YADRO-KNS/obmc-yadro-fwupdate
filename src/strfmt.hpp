/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 YADRO.
 */

#pragma once

#include <cstdio>
#include <string>

/**
 * @brief Create string by rules like in printf
 *
 * @param format - format string
 * @param args   - optional arguments
 *
 * @return formatted string
 */
template <typename... Args>
std::string strfmt(const char* format, Args&&... args)
{
    auto length = snprintf(nullptr, 0, format, std::forward<Args>(args)...);
    if (length <= 0)
    {
        return {};
    }

    std::string buf(length + 1 /* last null */, '\0');
    snprintf(buf.data(), buf.size(), format, std::forward<Args>(args)...);
    return buf;
}
