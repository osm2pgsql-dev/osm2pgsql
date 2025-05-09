#ifndef OSM2PGSQL_WILDCMP_HPP
#define OSM2PGSQL_WILDCMP_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2025 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

bool wild_match(char const *expr, char const *str) noexcept;

#endif // OSM2PGSQL_WILDCMP_HPP
