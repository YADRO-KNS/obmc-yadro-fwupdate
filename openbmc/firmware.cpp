/**
 * @brief OpenBMC firmware tools definitions.
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

#include "openbmc/firmware.hpp"

#include "utils/dbus.hpp"
#include "utils/tracer.hpp"

namespace openbmc
{

void lock(void)
{
    utils::tracer::trace_task("Locking BMC reboot",
                              std::bind(utils::startUnit, REBOOT_GUARD_ENABLE));
}

void unlock(void)
{
    utils::tracer::trace_task(
        "Unocking BMC reboot",
        std::bind(utils::startUnit, REBOOT_GUARD_DISABLE));
}

} // namespace openbmc
