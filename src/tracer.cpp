/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 YADRO.
 */

#include "config.h"

#include "tracer.hpp"

#include <stdio.h>
#include <unistd.h>

#include <exception>

namespace tracer
{

constexpr auto COLOR_RED = "\033[31;1m";
constexpr auto COLOR_GREEN = "\033[32m";
constexpr auto COLOR_RESET = "\033[0m";

bool is_tty(void)
{
    static const bool tty = isatty(fileno(stdout));
    return tty;
}

void print_result(const char* result, const char* color)
{
    fprintf(stdout, "[%s%s%s]\n", is_tty() ? color : "", result,
            is_tty() ? COLOR_RESET : "");
    fflush(stdout);
}

void done(void)
{
    print_result(" OK ", COLOR_GREEN);
}

void fail(void)
{
    print_result("FAIL", COLOR_RED);
}

void trace_task(const char* name, Task task)
{
    fprintf(stdout, "%s ... ", name);
    try
    {
        task();
        done();
    }
    catch (...)
    {
        fail();
        std::rethrow_exception(std::current_exception());
    }
}

} // namespace tracer
