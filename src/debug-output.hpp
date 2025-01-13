#ifndef OSM2PGSQL_DEBUG_OUTPUT_HPP
#define OSM2PGSQL_DEBUG_OUTPUT_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2025 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <vector>

class expire_output_t;
class flex_table_t;

void write_expire_output_list_to_debug_log(
    std::vector<expire_output_t> const &expire_outputs);

void write_table_list_to_debug_log(std::vector<flex_table_t> const &tables);

#endif // OSM2PGSQL_DEBUG_OUTPUT_HPP
