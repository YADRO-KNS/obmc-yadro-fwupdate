/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 YADRO.
 */

#pragma once

#include "config.h"

#include "fwupderr.hpp"

#include <sdbusplus/bus.hpp>

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
 * @return Return map of service -> implemented interfaces
 */
Objects getObjects(const Path& path, const Interfaces& ifaces);

/**
 * @brief Obtain a map of path -> services where path is in subtree and services
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
 * @brief Check if specified systemd unit exists.
 *
 * @param unitname - Name of the systemd unit.
 *
 * @return true if service exists.
 */
bool doesUnitExist(const std::string& unitname);

/**
 * @brief Start systemd unit.
 *
 * @param unitname - Name of the systemd unit.
 */
void startUnit(const std::string& unitname);

/**
 * @brief Stop systemd unit.
 *
 * @param unitname - Name of the systemd unit.
 */
void stopUnit(const std::string& unitname);

/**
 * @brief Bus handler singleton
 */
extern sdbusplus::bus::bus systemBus;

using PropertyName = std::string;

/**
 * @brief Get D-Bus property from specified service.
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
    auto req = systemBus.new_method_call(busname.c_str(), path.c_str(),
                                         SYSTEMD_PROPERTIES_INTERFACE, "Get");
    req.append(iface, property);
    std::variant<PropertyType> value;

    try
    {
        systemBus.call(req).read(value);
    }
    catch (const sdbusplus::exception::SdBusError& e)
    {
        throw FwupdateError("Get property call failed: %s", e.what());
    }

    return std::get<PropertyType>(value);
}

/**
 * @brief Set D-Bus property
 *
 * @param busname  - D-Bus service name
 * @param path     - Object path
 * @param iface    - Property interface
 * @param property - Property name
 * @param value    - Property value
 */
template <typename PropertyType>
void setProperty(const BusName& busname, const Path& path,
                 const Interface& iface, const PropertyName& property,
                 const PropertyType& value)
{
    auto req = systemBus.new_method_call(busname.c_str(), path.c_str(),
                                         SYSTEMD_PROPERTIES_INTERFACE, "Set");
    req.append(iface, property, std::variant<PropertyType>(value));

    try
    {
        systemBus.call_noreply(req);
    }
    catch (const sdbusplus::exception::SdBusError& e)
    {
        throw FwupdateError("Set property call failed: %s", e.what());
    }
}

/**
 * @brief Check whether the host is running
 *
 * @return - true if the Chassis is powered on
 */
bool isChassisOn();
