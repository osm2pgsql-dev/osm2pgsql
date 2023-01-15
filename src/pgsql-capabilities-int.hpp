#ifndef OSM2PGSQL_PGSQL_CAPABILITIES_INT_HPP
#define OSM2PGSQL_PGSQL_CAPABILITIES_INT_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "pgsql-capabilities.hpp"

#include <map>
#include <set>
#include <string>

struct database_capabilities_t
{
    std::map<std::string, std::string> settings;

    std::set<std::string> extensions;
    std::set<std::string> schemas;
    std::set<std::string> tablespaces;
    std::set<std::string> index_methods;

    std::string database_name;

    uint32_t database_version = 0;
    postgis_version postgis{};
};

database_capabilities_t &database_capabilities_for_testing() noexcept;

#endif // OSM2PGSQL_PGSQL_CAPABILITIES_INT_HPP
