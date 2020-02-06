/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 YADRO.
 */

#pragma once

#include "strfmt.hpp"

namespace subprocess
{

/**
 * @brief Check wait status and throw exception if child failed.
 *
 * @param wstatus - status returned by pclose()/system()/waitpid().
 */
void check_wait_status(int wstatus);

/**
 * @brief Execute the external command.
 *
 * @param cmd - Command and its arguments
 *
 * @return command output
 */
std::string exec(const char* cmd);

template <typename... Args>
std::string exec(const char* fmt, Args&&... args)
{
    return exec(strfmt(fmt, std::forward<Args>(args)...).c_str());
}

} // namespace subprocess
