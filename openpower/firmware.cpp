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

#include "openpower/firmware.hpp"

#include "utils/dbus.hpp"
#include "utils/subprocess.hpp"
#include "utils/tracer.hpp"

#include <map>

namespace openpower
{


// Map of PNOR partitions as partition name -> flag is it should use ECC clear
using PartsMap = std::map<std::string, bool>;

PartsMap getPartsToClear(const std::string& info)
{
    PartsMap ret;
    std::istringstream iss(info);
    std::string line;

    while (std::getline(iss, line))
    {
        // Each line looks like
        // ID=06 MVPD 0x0012d000..0x001bd000 (actual=0x00090000) [E--P--F-C-]
        // Flag 'F' means REPROVISION
        // Flag 'E' means ECC required
        auto pos = line.find('[');
        if (pos == std::string::npos)
        {
            continue;
        }
        auto flags = line.substr(pos);
        if (flags.find('F') != std::string::npos)
        {
            // This is a partition to be cleared
            pos = line.find_first_of(' '); // After "ID=xx"
            if (pos == std::string::npos)
            {
                continue;
            }

            pos = line.find_first_not_of(' ', pos); // After spaces
            if (pos == std::string::npos)
            {
                continue;
            }

            auto end = line.find_first_of(' ', pos); // The end of part name
            if (end == std::string::npos)
            {
                continue;
            }
            line = line.substr(pos, end - pos); // The part name

            ret[line] = flags.find('E') != std::string::npos;
        }
    }

    return ret;
}

// Get partitions that should be cleared
PartsMap getPartsToClear()
{
    const auto& [rc, pflashInfo] = utils::subprocess::exec(
        PFLASH_CMD, "-i 2>/dev/null | grep ^ID | grep 'F'");
    return getPartsToClear(pflashInfo);
}

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

void reset(void)
{
    auto partitions = getPartsToClear();
    if (partitions.empty())
    {
        fprintf(stdout, "NOTE: No partitions found the PNOR flash!\n");
    }

    for (auto p : partitions)
    {
        fprintf(stdout, "Clear %s partition [%s]... ", p.first.c_str(),
                p.second ? "ECC" : "Erase");
        try
        {
            int rc;
            std::tie(rc, std::ignore) = utils::subprocess::exec(
                PFLASH_CMD, "-P", p.first, p.second ? "-c" : "-e",
                "-f >/dev/null");
            utils::subprocess::check_wait_status(rc);
            utils::tracer::done();
        }
        catch (...)
        {
            utils::tracer::fail();
            throw std::runtime_error("Failed to reset PNOR flash.");
        }

    }
}

} // namespace openpower
