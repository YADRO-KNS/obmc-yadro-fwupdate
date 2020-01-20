/**
 * @brief D-Bus utils definitions.
 *
 * This file is part of OpenBMC/OpenPOWER firmware updater.
 *
 * Copyright 2020 YADRO
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "config.h"

#include "utils/dbus.hpp"

namespace utils
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
    catch (const sdbusplus::exception::SdBusError&)
    {
        throw std::runtime_error("Mapper GetObject call failed");
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
    catch (const sdbusplus::exception::SdBusError&)
    {
        throw std::runtime_error("Mapper GetSubTree call failed");
    }
    return objects;
}

bool startUnit(const std::string& unitname)
{
    auto req = bus.new_method_call(SYSTEMD_BUSNAME, SYSTEMD_PATH,
                                   SYSTEMD_INTERFACE, "StartUnit");
    req.append(unitname, "replace");
    try
    {
        bus.call(req);
    }
    catch (const sdbusplus::exception::SdBusError&)
    {
        return false;
    }

    return true;
}

} // namespace utils
