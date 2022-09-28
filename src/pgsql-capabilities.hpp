#ifndef OSM2PGSQL_PGSQL_CAPABILITIES_HPP
#define OSM2PGSQL_PGSQL_CAPABILITIES_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "pgsql.hpp"

#include <string>

bool has_extension(pg_conn_t const &db_connection, std::string const &value);
bool has_schema(pg_conn_t const &db_connection, std::string const &value);
bool has_tablespace(pg_conn_t const &db_connection, std::string const &value);

#endif // OSM2PGSQL_PGSQL_CAPABILITIES_HPP
