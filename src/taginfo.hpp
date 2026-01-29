#ifndef OSM2PGSQL_TAGINFO_HPP
#define OSM2PGSQL_TAGINFO_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2026 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <cstdint>
#include <string>
#include <vector>

enum class column_type_t : std::uint8_t
{
    INT,
    REAL,
    TEXT
};

struct column_t
{
    column_t(std::string n, std::string tn, column_type_t t)
    : name(std::move(n)), type_name(std::move(tn)), type(t)
    {}

    std::string name;
    std::string type_name;
    column_type_t type;
};

using columns_t = std::vector<column_t>;

#endif // OSM2PGSQL_TAGINFO_HPP
