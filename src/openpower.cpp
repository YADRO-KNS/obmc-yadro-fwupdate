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

#include <filesystem>
#include <map>
#include <regex>

namespace openpower
{

namespace fs = std::filesystem;

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
    return getPartsToClear(subprocess::exec("%s -i 2>/dev/null", PFLASH_CMD));
}

/**
 * @brief Get HIOMPAD bus name.
 */
static dbus::BusName hiomapd(void)
{
    static dbus::BusName bus;
    if (bus.empty())
    {
        try
        {
            auto objs = dbus::getObjects(HIOMAPD_PATH, {HIOMAPD_IFACE});
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
            throw FwupdateError("No hiomapd service found");
        }
    }

    return bus;
}

/**
 * @brief Get actual state of HIOMAPD.
 */
static uint8_t hiomapd_daemon_state(void)
{
    return dbus::getProperty<uint8_t>(hiomapd(), HIOMAPD_PATH, HIOMAPD_IFACE,
                                      "DaemonState");
}

// True if we've suspended HIOMAPD
static bool suspended = false;

void lock(void)
{
    Tracer tracer("Suspending HIOMAPD");

    if (hiomapd_daemon_state() == 0)
    {
        auto req = dbus::bus.new_method_call(hiomapd().c_str(), HIOMAPD_PATH,
                                             HIOMAPD_IFACE, "Suspend");
        dbus::bus.call(req);
        suspended = true;
    }
    else
    {
        throw FwupdateError("HIOMAPD already suspended");
    }

    tracer.done();
}

void unlock(void)
{
    Tracer tracer("Resuming HIOMAPD");

    if (suspended)
    {
        auto req = dbus::bus.new_method_call(hiomapd().c_str(), HIOMAPD_PATH,
                                             HIOMAPD_IFACE, "Resume");
        req.append(true);
        dbus::bus.call(req);
        suspended = false;
    }

    tracer.done();
}

void reset(void)
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
            std::ignore =
                subprocess::exec("%s -P %s -%c -f &>/dev/null", PFLASH_CMD,
                                 p.first.c_str(), p.second ? 'c' : 'e');
            tracer.done();
        }
    }
    catch (...)
    {
        throw FwupdateError("Failed to reset PNOR flash.");
    }
}

void flash(const Files& firmware, const fs::path& tmpdir)
{
    fs::path nvram(tmpdir);

    if (!nvram.empty())
    {
        Tracer tracer("Preserve NVRAM configuration");

        nvram /= "nvram.bin";
        std::ignore = subprocess::exec("%s -P NVRAM -r %s &>/dev/null",
                                       PFLASH_CMD, nvram.c_str());

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

    for (const auto& entry : firmware)
    {
        fprintf(stdout, "Writing %s ... \n", entry.filename().c_str());
        fflush(stdout);

        // NOTE: This process may take a lot of time and we want to show the
        //       progress from original pflash output.
        int rc =
            system(strfmt("%s -f -E -p %s", PFLASH_CMD, entry.c_str()).c_str());
        subprocess::check_wait_status(rc);
    }

    if (!nvram.empty() && fs::exists(nvram))
    {
        Tracer tracer("Recover NVRAM configuration");
        std::ignore = subprocess::exec("%s -f -e -P NVRAM -p %s", PFLASH_CMD,
                                       nvram.c_str());
        tracer.done();
    }
}

Files get_fw_files(const fs::path& dir)
{
    Files ret;
    for (const auto& p : fs::directory_iterator(dir))
    {
        if (p.is_regular_file() && p.path().extension() == ".pnor")
        {
            ret.emplace(dir / p.path());
        }
    }

    if (ret.empty())
    {
        throw FwupdateError("No OpenPOWER firmware files found!");
    }

    return ret;
}

} // namespace openpower
