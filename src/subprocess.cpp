/**
 * @brief Execute externel commands helper definitions.
 *
 * This file is part of OpenBMC/OpenPOWER firmware updater.
 *
 * Copyright 2020 YADRO
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
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
