/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 YADRO.
 */

#include "subprocess.hpp"

#include "fwupderr.hpp"

#include <array>
#include <cerrno>
#include <cstring>
#include <sstream>

namespace subprocess
{

void check_wait_status(int wstatus)
{
    if (WIFSIGNALED(wstatus))
    {
        throw FwupdateError("killed by signal %d.", WTERMSIG(wstatus));
    }

    if (!WIFEXITED(wstatus))
    {
        throw FwupdateError("unknown wait status: 0x%08X", wstatus);
    }

    int rc = WEXITSTATUS(wstatus);
    if (rc != EXIT_SUCCESS)
    {
        throw FwupdateError("exited with status %d", rc);
    }
}

std::string exec(const char* cmd)
{
    FILE* pipe = popen(cmd, "r");
    if (!pipe)
    {
        throw FwupdateError("popen() failed, error=%d: %s", errno,
                            strerror(errno));
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
        throw FwupdateError("pclose() failed, error=%d: %s", errno,
                            strerror(errno));
    }

    check_wait_status(rc);

    return result.str();
}

} // namespace subprocess
