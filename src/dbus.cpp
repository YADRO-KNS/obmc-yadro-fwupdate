/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 YADRO.
 */

#include "config.h"

#include "dbus.hpp"

namespace dbus
{

sdbusplus::bus::bus bus = sdbusplus::bus::new_default();

Objects getObjects(const Path& path, const Interfaces& ifaces)
{
    auto mapper = bus.new_method_call(MAPPER_BUSNAME, MAPPER_PATH,
                                      MAPPER_INTERFACE, "GetObject");
    mapper.append(path, ifaces);
    Objects objects;
    try
    {
        bus.call(mapper).read(objects);
    }
    catch (const sdbusplus::exception::SdBusError& e)
    {
        throw FwupdateError("Mapper GetObject call failed: %s", e.what());
    }
    return objects;
}

ObjectsMap getSubTree(const Path& path, const Interfaces& ifaces, int32_t depth)
{
    auto mapper = bus.new_method_call(MAPPER_BUSNAME, MAPPER_PATH,
                                      MAPPER_INTERFACE, "GetSubTree");
    mapper.append(path, depth, ifaces);
    ObjectsMap objects;
    try
    {
        bus.call(mapper).read(objects);
    }
    catch (const sdbusplus::exception::SdBusError& e)
    {
        throw FwupdateError("Mapper GetSubTree call failed: %s", e.what());
    }
    return objects;
}

void startUnit(const std::string& unitname)
{
    auto req = bus.new_method_call(SYSTEMD_BUSNAME, SYSTEMD_PATH,
                                   SYSTEMD_INTERFACE, "StartUnit");
    req.append(unitname, "replace");
    bus.call_noreply(req);
}

} // namespace dbus
