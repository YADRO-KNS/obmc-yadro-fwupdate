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

namespace openpower
{

namespace fs = std::filesystem;

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
    const auto& [rc, pflashInfo] =
        subprocess::exec(PFLASH_CMD, "-i 2>/dev/null | grep ^ID | grep 'F'");
    return getPartsToClear(pflashInfo);
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

/**
 * @brief Switch HIOMAPD to suspended state.
 */
static void hiomapd_suspend(void)
{
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
}

/**
 * @brief Restore normal state of HIOMAPD.
 */
static void hiomapd_resume(void)
{
    if (suspended)
    {
        auto req = dbus::bus.new_method_call(hiomapd().c_str(), HIOMAPD_PATH,
                                             HIOMAPD_IFACE, "Resume");
        req.append(true);
        dbus::bus.call(req);
        suspended = false;
    }
}

void lock(void)
{
    tracer::trace_task("Suspending HIOMAPD", hiomapd_suspend);
}

void unlock(void)
{
    tracer::trace_task("Resuming HIOMAPD", hiomapd_resume);
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
            std::tie(rc, std::ignore) =
                subprocess::exec(PFLASH_CMD, "-P", p.first,
                                 p.second ? "-c" : "-e", "-f >/dev/null");
            subprocess::check_wait_status(rc);
            tracer::done();
        }
        catch (...)
        {
            tracer::fail();
            throw FwupdateError("Failed to reset PNOR flash.");
        }
    }
}

struct NVRAMNotCreated : public std::runtime_error
{
    explicit NVRAMNotCreated() :
        std::runtime_error::runtime_error("NVRAM is not created!")
    {
    }
};

void flash(const Files& firmware, const fs::path& tmpdir)
{
    fs::path nvram(tmpdir);

    if (!nvram.empty())
    {
        nvram /= "nvram.bin";
        try
        {
            tracer::trace_task("Preserve NVRAM configuration", [&]() {
                int rc;
                std::tie(rc, std::ignore) =
                    subprocess::exec(PFLASH_CMD, "-P NVRAM -r", nvram);
                subprocess::check_wait_status(rc);
                if (!fs::exists(nvram))
                {
                    throw NVRAMNotCreated();
                }
            });
        }
        catch (const NVRAMNotCreated&)
        {
            fprintf(stdout, "NOTE: Preserving NVRAM failed, "
                            "default settings will be used.\n");
        }
    }

    for (const auto& entry : firmware)
    {
        fprintf(stdout, "Writing %s ... \n", entry.filename().c_str());
        fflush(stdout);

        // NOTE: This process may take a lot of time and we want to show the
        //       progress from original pflash output.
        int rc = system(concat_string(PFLASH_CMD, "-f -E -p", entry).c_str());
        subprocess::check_wait_status(rc);
    }

    if (!nvram.empty() && fs::exists(nvram))
    {
        tracer::trace_task("Recover NVRAM configuration", [&]() {
            int rc;
            std::tie(rc, std::ignore) =
                subprocess::exec(PFLASH_CMD, "-f -e -P NVRAM -p", nvram);
            subprocess::check_wait_status(rc);
        });
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
