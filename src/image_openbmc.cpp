/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 YADRO.
 */

#include "config.h"

#include "image_openbmc.hpp"

#include "dbus.hpp"
#include "fwupderr.hpp"
#include "subprocess.hpp"
#include "tracer.hpp"

#define ENV_FACTORY_RESET "openbmconce\\x3dfactory\\x2dreset"
#define SERVICE_FACTORY_RESET                                                  \
    "obmc-flash-bmc-setenv@" ENV_FACTORY_RESET ".service"

#include <regex>

void OBMCPhosphorImageUpdater::reset()
{
    Tracer tracer("Enable the BMC clean");
    startUnit(SERVICE_FACTORY_RESET);
    tracer.done();
}

void OBMCPhosphorImageUpdater::doInstall(const fs::path& file)
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

bool OBMCPhosphorImageUpdater::doAfterInstall(bool reset)
{
    bool installed = !files.empty();

    if (installed && reset)
    {
        Tracer tracer("Cleaning whitelist");
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

bool OBMCPhosphorImageUpdater::isFileFlashable(const fs::path& file) const
{
    static const std::regex image("^image-(bmc|kernel|rofs|rwfs|u-boot)$");
    return std::regex_match(file.filename().string(), image);
}
