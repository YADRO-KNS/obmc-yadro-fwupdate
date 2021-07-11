/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 YADRO.
 */

#include "fwupdbase.hpp"

#include "fwupderr.hpp"
#include "signature.hpp"
#include "tracer.hpp"

#include "dbus.hpp"

FwUpdBase::FwUpdBase(const fs::path& tmpdir) : tmpdir(tmpdir)
{
}

bool FwUpdBase::add(const fs::path& file)
{
    bool ret = fs::is_regular_file(file) && isFileFlashable(file);
    if (ret)
    {
        files.emplace_back(file);
    }
    return ret;
}

void FwUpdBase::verify(const fs::path& publicKey, const std::string& hashFunc)
{
    for (const auto& file : files)
    {
        Tracer tracer("Check signature for %s", file.filename().c_str());
        if (!verifyFile(publicKey, hashFunc, file))
        {
            throw FwupdateError("The %s signature verification failed!",
                                file.filename().c_str());
        }
        tracer.done();
    }
}

bool FwUpdBase::install(bool reset)
{
    if (files.empty())
    {
        // Nothing to install for this updater type.
        return false;
    }

    doBeforeInstall(reset);

    for (const auto& file : files)
    {
        doInstall(file);
    }

    return doAfterInstall(reset);
}

void FwUpdBase::updateDBusStoredVersion(const std::string& objectPath,
                                         const std::string& version)
{
    constexpr const char* settingsService = "xyz.openbmc_project.Settings";
    constexpr const char* dbusPropertiesInterface = "org.freedesktop.DBus.Properties";
    constexpr const char* versionInterface = "xyz.openbmc_project.Software.Version";
    constexpr const char* versionProperty = "Version";

    auto method = systemBus.new_method_call(settingsService, objectPath.c_str(),
                                            dbusPropertiesInterface, "Set");
    try
    {
        method.append(versionInterface, versionProperty,
                      std::variant<std::string>(version));
        systemBus.call_noreply(method);
    }
    catch (const sdbusplus::exception::SdBusError&)
    {
        throw FwupdateError("Error resetting version of the firmware.");
    }

    return;
}
