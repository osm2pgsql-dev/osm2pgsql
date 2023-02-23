#ifndef OSM2PGSQL_LUA_CONFIG_HPP
#define OSM2PGSQL_LUA_CONFIG_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <string>

struct lua_State;

void setup_lua_environment(lua_State *lua_state, std::string const &filename,
                           bool append_mode);

#endif // OSM2PGSQL_LUA_CONFIG_HPP
