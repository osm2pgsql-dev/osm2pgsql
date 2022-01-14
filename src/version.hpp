#ifndef OSM2PGSQL_VERSION_HPP
#define OSM2PGSQL_VERSION_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

char const *get_build_type() noexcept;
char const *get_osm2pgsql_version() noexcept;
char const *get_osm2pgsql_short_version() noexcept;
char const *get_minimum_postgresql_server_version() noexcept;
unsigned long get_minimum_postgresql_server_version_num() noexcept;

#endif // OSM2PGSQL_VERSION_HPP
