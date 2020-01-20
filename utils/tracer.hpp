/**
 * @brief Tracer util declarations.
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

#pragma once

#include <functional>

namespace utils
{
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
} // namespace utils
