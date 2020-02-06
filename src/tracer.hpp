/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 YADRO.
 */

#pragma once

#include <unistd.h>

#include <cstdio>

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
        fprintf(stdout, fmt, std::forward<Args>(args)...);
        fprintf(stdout, " ... ");
        fflush(stdout);
    }
    Tracer(const char* msg)
    {
        fprintf(stdout, "%s ... ", msg);
        fflush(stdout);
    }

    /**
     * @brief Complete task with state 'success'
     */
    void done(void)
    {
        complete(" OK ", COLOR_GREEN);
    }

    /**
     * @brief Complete trace with status 'fail'
     */
    void fail(void)
    {
        complete("FAIL", COLOR_RED);
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
     * @return True if terminal is interactive.
     */
    bool is_tty(void) const
    {
        static const bool tty = isatty(fileno(stdout));
        return tty;
    }

    /**
     * @brief Complete trace and print status
     */
    void complete(const char* status, const char* color)
    {
        fprintf(stdout, "[%s%s%s]\n", is_tty() ? color : "", status,
                is_tty() ? COLOR_DEFAULT : "");
        fflush(stdout);
        completed = true;
    }

    bool completed = false;

    static constexpr auto COLOR_RED = "\033[31;1m";
    static constexpr auto COLOR_GREEN = "\033[32m";
    static constexpr auto COLOR_DEFAULT = "\033[0m";
};
