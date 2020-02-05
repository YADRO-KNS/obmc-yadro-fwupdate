/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 YADRO.
 */

#pragma once

#include <functional>

namespace tracer
{

using Task = std::function<void()>;

/**
 * @brief Print OK in green color if supported.
 */
void done(void);

/**
 * @brief Print FAIL in red color if supported.
 */
void fail(void);

/**
 * @brief Print task name, execute task and print colorized result.
 *
 * @param name - Task name
 * @param task - Task action
 */
void trace_task(const char* name, Task task);

} // namespace tracer
