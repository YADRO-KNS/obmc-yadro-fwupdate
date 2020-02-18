/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 YADRO.
 */

#include "config.h"

#include "openpower.hpp"

#include "dbus.hpp"
#include "fwupderr.hpp"
#include "subprocess.hpp"
#include "tracer.hpp"

#include <map>
#include <regex>

// Map of PNOR partitions as partition name -> flag is it should use ECC clear
using PartsMap = std::map<std::string, bool>;

static const std::regex PnorPartInfo("^ID=\\d+\\s+(\\w+)\\s.*\\[([^\\]]+)\\]$");

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

        std::smatch match;
        if (std::regex_match(line, match, PnorPartInfo))
        {
            const auto& name = match[1].str();
            const auto& flags = match[2].str();

            if (flags.find('F') != std::string::npos)
            {
                ret[name] = flags.find('E') != std::string::npos;
            }
        }
    }

    return ret;
}

// Get partitions that should be cleared
PartsMap getPartsToClear()
{
    return getPartsToClear(exec("%s -i 2>/dev/null", PFLASH_CMD));
}

/**
 * @brief Get HIOMPAD bus name.
 */
static BusName hiomapd()
{
    static BusName hiomapdBus;
    if (hiomapdBus.empty())
    {
        try
        {
            auto objs = getObjects(HIOMAPD_PATH, {HIOMAPD_IFACE});
            for (auto& obj : objs)
            {
                hiomapdBus = std::move(obj.first);
                break;
            }
        }
        catch (const std::runtime_error&)
        {
            hiomapdBus.clear();
        }

        if (hiomapdBus.empty())
        {
            throw FwupdateError("No hiomapd service found");
        }
    }

    return hiomapdBus;
}

/**
 * @brief Get actual state of HIOMAPD.
 */
static uint8_t hiomapd_daemon_state()
{
    return getProperty<uint8_t>(hiomapd(), HIOMAPD_PATH, HIOMAPD_IFACE,
                                "DaemonState");
}

/**
 * @brief Check whether the host is running
 *
 * @return - true if the Chassis is powered on
 */
static bool is_chassis_on()
{
    auto objs = getObjects(CHASSIS_STATE_PATH, {CHASSIS_STATE_IFACE});
    auto state =
        getProperty<std::string>(objs.begin()->first, CHASSIS_STATE_PATH,
                                 CHASSIS_STATE_IFACE, "CurrentPowerState");

    return state != CHASSIS_STATE_OFF;
}

void OpenPowerUpdater::lock()
{
    Tracer tracer("Suspending HIOMAPD");

    if (is_chassis_on())
    {
        throw FwupdateError("The host is running now, operation cancelled");
    }

    if (hiomapd_daemon_state() == 0)
    {
        auto req = systemBus.new_method_call(hiomapd().c_str(), HIOMAPD_PATH,
                                             HIOMAPD_IFACE, "Suspend");
        systemBus.call(req);
        locked = true;
    }
    else
    {
        throw FwupdateError("HIOMAPD already suspended");
    }

    tracer.done();
}

void OpenPowerUpdater::unlock()
{
    if (locked)
    {
        Tracer tracer("Resuming HIOMAPD");

        auto req = systemBus.new_method_call(hiomapd().c_str(), HIOMAPD_PATH,
                                             HIOMAPD_IFACE, "Resume");
        req.append(true);
        systemBus.call(req);
        locked = false;

        tracer.done();
    }
}

void OpenPowerUpdater::reset()
{
    auto partitions = getPartsToClear();
    if (partitions.empty())
    {
        fprintf(stdout, "NOTE: No partitions found the PNOR flash!\n");
    }

    try
    {
        for (auto p : partitions)
        {
            Tracer tracer("Clear %s partition [%s]", p.first.c_str(),
                          p.second ? "ECC" : "Erase");
            std::ignore = exec("%s -P %s -%c -f &>/dev/null", PFLASH_CMD,
                               p.first.c_str(), p.second ? 'c' : 'e');
            tracer.done();
        }
    }
    catch (...)
    {
        throw FwupdateError("Failed to reset PNOR flash.");
    }
}

void OpenPowerUpdater::do_before_install(bool reset)
{
    if (!files.empty() && !reset)
    {
        Tracer tracer("Preserve NVRAM configuration");

        auto nvram(tmpdir / "nvram.bin");
        std::ignore =
            exec("%s -P NVRAM -r %s &>/dev/null", PFLASH_CMD, nvram.c_str());

        if (!fs::exists(nvram))
        {
            tracer.fail();
            fprintf(stdout, "NOTE: Preserving NVRAM failed, "
                            "default settings will be used.\n");
        }
        else
        {
            tracer.done();
        }
    }
}

void OpenPowerUpdater::do_install(const fs::path& file)
{
    // NOTE: This process may take a lot of time and we want to show the
    //       progress from original pflash output.
    fprintf(stdout, "Writing %s ... \n", file.filename().c_str());
    fflush(stdout);
    int rc = system(strfmt("%s -f -E -p %s", PFLASH_CMD, file.c_str()).c_str());
    check_wait_status(rc);
}

bool OpenPowerUpdater::do_after_install(bool reset)
{
    auto nvram(tmpdir / "nvram.bin");
    if (!reset && fs::exists(nvram) && !files.empty())
    {
        Tracer tracer("Recover NVRAM configuration");
        std::ignore =
            exec("%s -f -e -P NVRAM -p %s", PFLASH_CMD, nvram.c_str());
        tracer.done();
    }
    return false;
}

bool OpenPowerUpdater::is_file_belong(const fs::path& file) const
{
    return file.extension() == ".pnor";
}
