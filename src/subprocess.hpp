/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 YADRO.
 */

#pragma once
#include <sstream>
#include <string>

template <typename... Ts>
std::string concat_string(Ts const&... ts)
{
    std::stringstream s;
    ((s << ts << " "), ...);
    return s.str();
}

namespace subprocess
{

/**
 * @brief Check wait status.
 *
 * @param wstatus - status returned by pclose()/system()/waitpid().
 */
void check_wait_status(int wstatus);

/**
 * @brief Execute the external command
 *
 * @param cmd - Command and its arguments
 *
 * @return wait statue and command output
 */
std::pair<int, std::string> exec(const std::string& cmd);

template <typename... Args>
std::pair<int, std::string> exec(const std::string& cmd, Args const&... args)
{
    return exec(concat_string(cmd, args...));
}

} // namespace subprocess
