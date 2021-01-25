/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 YADRO.
 */

#include "config.h"

#include "image_openpower.hpp"

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
            hiomapdBus = objs.begin()->first;
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
static uint8_t hiomapDaemonState()
{
    return getProperty<uint8_t>(hiomapd(), HIOMAPD_PATH, HIOMAPD_IFACE,
                                "DaemonState");
}

void OpenPowerUpdater::lock()
{
    Tracer tracer("Suspending HIOMAPD");

    if (isChassisOn())
    {
        throw FwupdateError("The host is running now, operation cancelled");
    }

    if (hiomapDaemonState() == 0)
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
        printf("NOTE: No partitions found on the PNOR flash!\n");
    }

    for (auto p : partitions)
    {
        Tracer tracer("Clear %s partition [%s]", p.first.c_str(),
                      p.second ? "ECC" : "Erase");
        std::ignore = exec("%s -P %s -%c -f 2>&1", PFLASH_CMD, p.first.c_str(),
                           p.second ? 'c' : 'e');
        tracer.done();
    }
}

static const std::vector<std::string> partsToPreserve = {
    "NVRAM",
};

void OpenPowerUpdater::doBeforeInstall(bool reset)
{
    if (!files.empty() && !reset)
    {
        for (const auto& part : partsToPreserve)
        {
            Tracer tracer("Preserve %s configuration", part.c_str());

            auto partFile(tmpdir / part);
            std::ignore = exec("%s -P %s -r %s 2>&1", PFLASH_CMD, part.c_str(),
                               partFile.c_str());

            if (!fs::exists(partFile))
            {
                tracer.fail();
                printf("NOTE: Preserving %s failed, default settings will be "
                       "used.\n",
                       part.c_str());
            }
            else
            {
                tracer.done();
            }
        }
    }
}

void OpenPowerUpdater::doInstall(const fs::path& file)
{
    // NOTE: This process may take a lot of time and we want to show the
    //       progress from original pflash output.
    printf("Writing %s ... \n", file.filename().c_str());
    int rc = system(strfmt("%s -f -E -p %s", PFLASH_CMD, file.c_str()).c_str());
    checkWaitStatus(rc, "");
}

bool OpenPowerUpdater::doAfterInstall(bool reset)
{
    if (!reset && !files.empty())
    {
        for (const auto& part : partsToPreserve)
        {
            auto partFile(tmpdir / part);
            if (fs::exists(partFile))
            {
                Tracer tracer("Recover %s configuration", part.c_str());
                std::ignore = exec("%s -f -e -P %s -p %s 2>&1", PFLASH_CMD,
                                   part.c_str(), partFile.c_str());
                tracer.done();
            }
        }
    }
    return false;
}

bool OpenPowerUpdater::isFileFlashable(const fs::path& file) const
{
    return file.extension() == ".pnor";
}
