#ifndef OSM2PGSQL_TAGINFO_HPP
#define OSM2PGSQL_TAGINFO_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <string>
#include <vector>

enum class ColumnType
{
    INT,
    REAL,
    TEXT
};

struct Column
{
    Column(std::string n, std::string tn, ColumnType t)
    : name(std::move(n)), type_name(std::move(tn)), type(t)
    {}

    std::string name;
    std::string type_name;
    ColumnType type;
};

using columns_t = std::vector<Column>;

#endif // OSM2PGSQL_TAGINFO_HPP
