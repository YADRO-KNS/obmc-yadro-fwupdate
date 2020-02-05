/**
 * @brief Tracer util definitions.
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
