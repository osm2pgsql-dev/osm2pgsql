#ifndef OSM2PGSQL_FLEX_DEBUG_OUTPUT_HPP
#define OSM2PGSQL_FLEX_DEBUG_OUTPUT_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "flex-table.hpp"
#include "flex-tileset.hpp"

void write_tileset_list_to_debug_log(
    std::vector<flex_tileset_t> const &tilesets);

void write_table_list_to_debug_log(std::vector<flex_table_t> const &tables,
                                   std::vector<flex_tileset_t> const &tilesets);

#endif // OSM2PGSQL_FLEX_DEBUG_OUTPUT_HPP
