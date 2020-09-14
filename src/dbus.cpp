/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 YADRO.
 */

#include "config.h"

#include "dbus.hpp"

#include <sdbusplus/bus/match.hpp>

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

bool doesUnitExist(const std::string& unitname)
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

void subscribeToSystemdSignals()
{
    auto req = systemBus.new_method_call(SYSTEMD_BUSNAME, SYSTEMD_PATH,
                                         SYSTEMD_INTERFACE, "Subscribe");
    systemBus.call_noreply(req);
}

void unsubscribeFromSystemdSignals()
{
    auto req = systemBus.new_method_call(SYSTEMD_BUSNAME, SYSTEMD_PATH,
                                         SYSTEMD_INTERFACE, "Unsubscribe");
    systemBus.call_noreply(req);
}

void stopUnit(const std::string& unitname)
{
    if (doesUnitExist(unitname))
    {
        bool serviceStopped = false;
        sdbusplus::bus::match_t systemdSignals(
            systemBus,
            sdbusplus::bus::match::rules::type::signal() +
                sdbusplus::bus::match::rules::member("JobRemoved") +
                sdbusplus::bus::match::rules::path(SYSTEMD_PATH) +
                sdbusplus::bus::match::rules::interface(SYSTEMD_INTERFACE),
            [&](sdbusplus::message::message& msg) {
                uint32_t newStateID;
                sdbusplus::message::object_path newStateObjPath;
                std::string newStateUnit;
                std::string newStateResult;

                msg.read(newStateID, newStateObjPath, newStateUnit,
                         newStateResult);
                if (newStateUnit == unitname && newStateResult == "done")
                {
                    serviceStopped = true;
                }
            });

        subscribeToSystemdSignals();

        auto req = systemBus.new_method_call(SYSTEMD_BUSNAME, SYSTEMD_PATH,
                                             SYSTEMD_INTERFACE, "StopUnit");
        req.append(unitname, "replace");
        systemBus.call_noreply(req);

        while (!serviceStopped)
        {
            systemBus.process_discard();
            if (!serviceStopped)
            {
                systemBus.wait();
            }
        }

        unsubscribeFromSystemdSignals();
    }
}
