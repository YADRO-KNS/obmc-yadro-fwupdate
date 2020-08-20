/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 YADRO.
 */

#include "config.h"

#include "dbus.hpp"

sdbusplus::bus::bus systemBus = sdbusplus::bus::new_default();

Objects getObjects(const Path& path, const Interfaces& ifaces)
{
    auto mapper = systemBus.new_method_call(MAPPER_BUSNAME, MAPPER_PATH,
                                            MAPPER_INTERFACE, "GetObject");
    mapper.append(path, ifaces);
    Objects objects;
    try
    {
        systemBus.call(mapper).read(objects);
    }
    catch (const sdbusplus::exception::SdBusError& e)
    {
        throw FwupdateError("Mapper GetObject call failed: %s", e.what());
    }
    return objects;
}

ObjectsMap getSubTree(const Path& path, const Interfaces& ifaces, int32_t depth)
{
    auto mapper = systemBus.new_method_call(MAPPER_BUSNAME, MAPPER_PATH,
                                            MAPPER_INTERFACE, "GetSubTree");
    mapper.append(path, depth, ifaces);
    ObjectsMap objects;
    try
    {
        systemBus.call(mapper).read(objects);
    }
    catch (const sdbusplus::exception::SdBusError& e)
    {
        throw FwupdateError("Mapper GetSubTree call failed: %s", e.what());
    }
    return objects;
}

void startUnit(const std::string& unitname)
{
    auto req = systemBus.new_method_call(SYSTEMD_BUSNAME, SYSTEMD_PATH,
                                         SYSTEMD_INTERFACE, "StartUnit");
    req.append(unitname, "replace");
    systemBus.call_noreply(req);
}

bool isUnitStarted(const std::string& unitname)
{
    auto req = systemBus.new_method_call(SYSTEMD_BUSNAME, SYSTEMD_PATH,
                                         SYSTEMD_INTERFACE, "GetUnit");
    req.append(unitname);
    try
    {
        systemBus.call_noreply(req);
    }
    catch (const sdbusplus::exception::SdBusError&)
    {
        return false;
    }

    return true;
}

void stopUnit(const std::string& unitname)
{
    if (isUnitStarted(unitname))
    {
        auto req = systemBus.new_method_call(SYSTEMD_BUSNAME, SYSTEMD_PATH,
                                             SYSTEMD_INTERFACE, "StopUnit");
        req.append(unitname, "replace");
        systemBus.call_noreply(req);
    }
}
