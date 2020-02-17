/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 YADRO.
 */

#include "config.h"

#include "openbmc.hpp"

#include "dbus.hpp"
#include "fwupderr.hpp"
#include "subprocess.hpp"
#include "tracer.hpp"

#define ENV_FACTORY_RESET "openbmconce\\x3dfactory\\x2dreset"
#define SERVICE_FACTORY_RESET                                                  \
    "obmc-flash-bmc-setenv@" ENV_FACTORY_RESET ".service"

#include <regex>

namespace openbmc
{

void OpenBmcUpdater::lock()
{
    Tracer tracer("Locking BMC reboot");
    dbus::startUnit(REBOOT_GUARD_ENABLE);
    locked = true;
    tracer.done();
}

void OpenBmcUpdater::unlock()
{
    if (locked)
    {
        Tracer tracer("Unocking BMC reboot");
        dbus::startUnit(REBOOT_GUARD_DISABLE);
        locked = false;
        tracer.done();
    }
}

void OpenBmcUpdater::reset()
{
    Tracer tracer("Enable the BMC clean");
    dbus::startUnit(SERVICE_FACTORY_RESET);
    tracer.done();
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

bool OpenBmcUpdater::do_after_install(bool reset)
{
    bool installed = !files.empty();

    if (installed && reset)
    {
        Tracer tracer("Cleaninig whitelist");
        fs::path whitelist(OPENBMC_FLASH_PATH);
        whitelist /= OPENBMC_WHITELIST_FILE_NAME;
        if (fs::exists(whitelist))
        {
            fs::resize_file(whitelist, 0);
        }
        tracer.done();
    }

    return installed;
}

bool OpenBmcUpdater::is_file_belongs(const fs::path& file) const
{
    static const std::regex image("^image-(bmc|kernel|rofs|rwfs|u-boot)$");
    return std::regex_match(file.filename().string(), image);
}

} // namespace openbmc
