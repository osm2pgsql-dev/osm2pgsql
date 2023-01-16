#ifndef OSM2PGSQL_PGSQL_CAPABILITIES_HPP
#define OSM2PGSQL_PGSQL_CAPABILITIES_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <string>

class pg_conn_t;

void init_database_capabilities(pg_conn_t const &db_connection);

bool has_extension(std::string const &value);
bool has_schema(std::string const &value);
bool has_tablespace(std::string const &value);
bool has_index_method(std::string const &value);

/// Get PostgreSQL version in the format (major * 10000 + minor).
uint32_t get_database_version() noexcept;

struct postgis_version
{
    int major;
    int minor;
};

/// Get PostGIS major and minor version.
postgis_version get_postgis_version() noexcept;

#endif // OSM2PGSQL_PGSQL_CAPABILITIES_HPP
