#ifndef OSM2PGSQL_FLEX_LUA_INDEX_HPP
#define OSM2PGSQL_FLEX_LUA_INDEX_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

/**
 * \file
 *
 * Functions implementing the Lua interface for index creation.
 */

struct lua_State;
class flex_table_t;

void flex_lua_setup_index(lua_State *lua_state, flex_table_t *table);

#endif // OSM2PGSQL_FLEX_LUA_INDEX_HPP
