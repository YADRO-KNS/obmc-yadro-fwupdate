/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 YADRO.
 */

#include "subprocess.hpp"

#include <array>
#include <cerrno>
#include <cstdio>

namespace subprocess
{

void check_wait_status(int wstatus)
{
    std::string buf(32, '\0');

    if (WIFSIGNALED(wstatus))
    {
        snprintf(buf.data(), buf.size(), "killed by signal %d",
                 WTERMSIG(wstatus));
        throw std::runtime_error(buf);
    }

    if (!WIFEXITED(wstatus))
    {
        snprintf(buf.data(), buf.size(), "unknown wait status: 0x%08X",
                 wstatus);
        throw std::runtime_error(buf);
    }

    int rc = WEXITSTATUS(wstatus);
    if (rc != EXIT_SUCCESS)
    {
        snprintf(buf.data(), buf.size(), "Exit with status %d", rc);
        throw std::runtime_error(buf);
    }
}

std::pair<int, std::string> exec(const std::string& cmd)
{
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe)
    {
        throw std::system_error(errno, std::generic_category());
    }

    std::array<char, 512> buffer;
    std::stringstream result;
    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr)
    {
        result << buffer.data();
    }

    int rc = pclose(pipe);
    if (rc == -1)
    {
        throw std::system_error(errno, std::generic_category());
    }

    return {rc, result.str()};
}

} // namespace subprocess
