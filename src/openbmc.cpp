/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 YADRO.
 */

#include "config.h"

#include "openbmc.hpp"

#include "confirm.hpp"
#include "dbus.hpp"
#include "fwupderr.hpp"
#include "subprocess.hpp"
#include "tracer.hpp"

#include <exception>
#include <functional>

#define ENV_FACTORY_RESET "openbmconce\\x3dfactory\\x2dreset"
#define SERVICE_FACTORY_RESET                                                  \
    "obmc-flash-bmc-setenv@" ENV_FACTORY_RESET ".service"

namespace openbmc
{

void lock(void)
{
    Tracer tracer("Locking BMC reboot");
    dbus::startUnit(REBOOT_GUARD_ENABLE);
    tracer.done();
}

void unlock(void)
{
    Tracer tracer("Unocking BMC reboot");
    dbus::startUnit(REBOOT_GUARD_DISABLE);
    tracer.done();
}

void reset(void)
{
    Tracer tracer("Enable the BMC clean");
    dbus::startUnit(SERVICE_FACTORY_RESET);
    tracer.done();
}

void flash(const Files& firmware, bool reset)
{
    fs::path initramfs(OPENBMC_FLASH_PATH);
    for (const auto& entry : firmware)
    {
        Tracer tracer("Install %s", entry.filename().c_str());

        fs::path destination(initramfs / entry.filename());
        if (fs::exists(destination))
        {
            fs::remove_all(destination);
        }

        fs::copy(entry, destination);

        tracer.done();
    }

    if (reset)
    {
        Tracer tracer("Cleaning whitelist");
        fs::resize_file(initramfs / OPENBMC_WHITELIST_FILE_NAME, 0);
        tracer.done();
    }
}

void reboot(bool interactive)
{
    bool manual_reboot = false;
    if (interactive &&
        !confirm("The BMC system will be rebooted to apply changes."))
    {
        manual_reboot = true;
    }

    try
    {
        if (!manual_reboot)
        {
            Tracer tracer("Reboot BMC system");
            std::ignore = subprocess::exec("/sbin/reboot");
            tracer.done();
        }
    }
    catch (...)
    {
        manual_reboot = true;
    }

    if (manual_reboot)
    {
        throw FwupdateError("The BMC needs to be manually rebooted.");
    }
}

Files get_fw_files(const fs::path& dir)
{
    Files ret;
    bool full = false;
    for (const auto& p : fs::directory_iterator(dir))
    {
        if (p.is_regular_file() && p.path().extension() != SIGNATURE_FILE_EXT &&
            p.path().filename().string().compare(0, 6, "image-") == 0)
        {
            if (p.path().filename() == "image-bmc")
            {
                full = true;
            }

            ret.emplace(dir / p.path());
        }
    }

    if (ret.empty())
    {
        throw FwupdateError("No OpenBMC firmware files found!");
    }

    if (full && ret.size() != 1)
    {
        throw FwupdateError(
            "Firmware package contains overlapped OpenBMC parts!");
    }

    return ret;
}

} // namespace openbmc
