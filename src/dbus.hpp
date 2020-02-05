/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 YADRO.
 */

#pragma once

#include "config.h"

#include <sdbusplus/bus.hpp>

namespace dbus
{

using BusName = std::string;
using Path = std::string;
using Interface = std::string;
using Interfaces = std::vector<Interface>;
using Objects = std::map<BusName, Interfaces>;
using ObjectsMap = std::map<Path, Objects>;

/**
 * @brief Gets the list of the D-Bus objects for the input D-Bus path and which
 * implemented the specified interfaces.
 *
 * @param path   - Object path
 * @param ifaces - Interfaces
 *
 * @return Return map of sevice -> implemented interfaces
 */
Objects getObjects(const Path& path, const Interfaces& ifaces);

/**
 * @brief Obtain a map of path -> services where path is in subtree and serivces
 * is of the type returned by the getObjects method
 *
 * @param path   - Objects path
 * @param ifaces - Interfaces
 * @param depth  - The maximum subtree depth
 *
 * @return The map of path -> services
 */
ObjectsMap getSubTree(const Path& path, const Interfaces& ifaces,
                      int32_t depth = 0);

/**
 * @brief Start systemd unit.
 *
 * @param unitname - Name of the sustemd unit.
 *
 * @return true if unit success started.
 */
bool startUnit(const std::string& unitname);

/**
 * @brief Bus handler singleton
 */
extern sdbusplus::bus::bus bus;

using PropertyName = std::string;

/**
 * @brief Get D-Bus propery from specified service.
 *
 * @param busname  - D-Bus service name
 * @param path     - Object path
 * @param iface    - Property interface
 * @param property - Property name
 *
 * @return Property value
 */
template <typename PropertyType>
PropertyType getProperty(const BusName& busname, const Path& path,
                         const Interface& iface, const PropertyName& property)
{
    auto req = bus.new_method_call(busname.c_str(), path.c_str(),
                                   SYSTEMD_PROPERTIES_INTERFACE, "Get");
    req.append(iface, property);
    std::variant<PropertyType> value;

    try
    {
        bus.call(req).read(value);
    }
    catch (const sdbusplus::exception::SdBusError&)
    {
        throw std::runtime_error("Get property call failed");
    }

    return std::get<PropertyType>(value);
}

} // namespace dbus
