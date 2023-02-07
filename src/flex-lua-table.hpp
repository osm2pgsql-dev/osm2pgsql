#ifndef OSM2PGSQL_FLEX_LUA_TABLE_HPP
#define OSM2PGSQL_FLEX_LUA_TABLE_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <vector>

class flex_table_t;
struct lua_State;

static char const *const osm2pgsql_table_name = "osm2pgsql.Table";

int setup_flex_table(lua_State *lua_state, std::vector<flex_table_t> *tables,
                     bool updatable);

#endif // OSM2PGSQL_FLEX_LUA_TABLE_HPP
