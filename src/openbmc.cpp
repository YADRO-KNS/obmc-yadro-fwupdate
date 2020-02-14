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

Lock::Lock()
{
    Tracer tracer("Locking BMC reboot");
    dbus::startUnit(REBOOT_GUARD_ENABLE);
    tracer.done();
}

Lock::~Lock()
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

void OpenBmcUpdater::do_install(const fs::path& file)
{
    Tracer tracer("Install %s", file.filename().c_str());

    fs::path destination(OPENBMC_FLASH_PATH);
    destination /= file.filename();

    if (fs::exists(destination))
    {
        fs::remove_all(destination);
    }
    fs::copy(file, destination);

    tracer.done();
}

void OpenBmcUpdater::do_after_install(bool reset)
{
    if (reset)
    {
        Tracer tracer("Cleaninig whitelist");
        fs::path whitelist(OPENBMC_FLASH_PATH);
        whitelist /= OPENBMC_WHITELIST_FILE_NAME;
        fs::resize_file(whitelist, 0);
        tracer.done();
    }
}

} // namespace openbmc
