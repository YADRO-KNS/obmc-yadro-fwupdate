/**
 * @brief OpenPOWER firmware tools definitions.
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

#include "utils/dbus.hpp"
#include "utils/tracer.hpp"

namespace openpower
{

/**
 * @brief Get HIOMPAD bus name.
 */
static utils::BusName hiomapd(void)
{
    static utils::BusName bus;
    if (bus.empty())
    {
        try
        {
            auto objs = utils::getObjects(HIOMAPD_PATH, {HIOMAPD_IFACE});
            for (auto& obj : objs)
            {
                bus = std::move(obj.first);
                break;
            }
        }
        catch (const std::runtime_error&)
        {
            bus.clear();
        }

        if (bus.empty())
        {
            throw std::runtime_error("No hiomapd service found");
        }
    }

    return bus;
}

/**
 * @brief Get actual state of HIOMAPD.
 */
static uint8_t hiomapd_daemon_state(void)
{
    return utils::getProperty<uint8_t>(hiomapd(), HIOMAPD_PATH, HIOMAPD_IFACE,
                                       "DaemonState");
}

// True if we've suspended HIOMAPD
static bool suspended = false;

/**
 * @brief Switch HIOMAPD to suspended state.
 */
static void hiomapd_suspend(void)
{
    if (hiomapd_daemon_state() == 0)
    {
        auto req = utils::bus.new_method_call(hiomapd().c_str(), HIOMAPD_PATH,
                                              HIOMAPD_IFACE, "Suspend");
        utils::bus.call(req);
        suspended = true;
    }
    else
    {
        throw std::runtime_error("HIOMAPD already suspended");
    }
}

/**
 * @brief Restore normal state of HIOMAPD.
 */
static void hiomapd_resume(void)
{
    if (suspended)
    {
        auto req = utils::bus.new_method_call(hiomapd().c_str(), HIOMAPD_PATH,
                                              HIOMAPD_IFACE, "Resume");
        req.append(true);
        utils::bus.call(req);
        suspended = false;
    }
}

void lock(void)
{
    utils::tracer::trace_task("Suspending HIOMAPD", hiomapd_suspend);
}

void unlock(void)
{
    utils::tracer::trace_task("Resuming HIOMAPD", hiomapd_resume);
}

} // namespace openpower
