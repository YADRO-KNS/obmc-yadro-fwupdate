/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 YADRO.
 */

#pragma once

#include <cstdio>
#include <cstring>

constexpr auto TITLE_WIDTH = 40;

/**
 * @brief RAII task tracer.
 *        It print task title and wait for completion.
 *        If completion wouldn't called this task show fail state.
 */
struct Tracer
{

    Tracer() = delete;
    Tracer(const Tracer&) = delete;
    Tracer& operator=(const Tracer&) = delete;

    /**
     * @brief Start task tracing.
     *
     * @param fmt  - printf like string format
     * @param args - optional arguments
     */
    template <typename... Args>
    Tracer(const char* fmt, Args&&... args)
    {
        int offset = fprintf(stdout, fmt, std::forward<Args>(args)...);
        fprintf(stdout, " %*s ", offset - TITLE_WIDTH, "...");
        fflush(stdout);
    }
    Tracer(const char* msg)
    {
        int offset = strlen(msg);
        fprintf(stdout, "%s %*s ", msg, offset - TITLE_WIDTH, "...");
        fflush(stdout);
    }

    /**
     * @brief Complete task with state 'success'
     */
    void done()
    {
        complete(" OK ");
    }

    /**
     * @brief Complete trace with status 'fail'
     */
    void fail()
    {
        complete("FAIL");
    }

    ~Tracer()
    {
        if (!completed)
        {
            fail();
        }
    }

  private:
    /**
     * @brief Complete trace and print status
     */
    void complete(const char* status)
    {
        fprintf(stdout, "[%s]\n", status);
        fflush(stdout);
        completed = true;
    }

    bool completed = false;
};
