/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 YADRO.
 */

#pragma once

#include "strfmt.hpp"

/**
 * @brief Check wait status and throw exception if child failed.
 *
 * @param wstatus - status returned by pclose()/system()/waitpid().
 * @param output  - last command output to include in exception message.
 */
void checkWaitStatus(int wstatus, const std::string& output);

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
