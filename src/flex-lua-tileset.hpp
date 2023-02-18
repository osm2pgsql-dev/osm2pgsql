#ifndef OSM2PGSQL_FLEX_LUA_TILESET_HPP
#define OSM2PGSQL_FLEX_LUA_TILESET_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "expire-tiles.hpp"

struct lua_State;

static char const *const osm2pgsql_tileset_name = "osm2pgsql.Tileset";

int setup_flex_tileset(lua_State *lua_state,
                       std::vector<expire_tiles> *tilesets);

#endif // OSM2PGSQL_FLEX_LUA_TILESET_HPP
