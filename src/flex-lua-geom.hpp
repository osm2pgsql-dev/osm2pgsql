#ifndef OSM2PGSQL_FLEX_LUA_GEOM_HPP
#define OSM2PGSQL_FLEX_LUA_GEOM_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

/**
 * \file
 *
 * Functions implementing the Lua interface for the geometry functions.
 */

#include "geom.hpp"

struct lua_State;

/**
 * Create a null geometry object on the Lua stack and return a pointer to it.
 */
geom::geometry_t *create_lua_geometry_object(lua_State *lua_state);

/**
 * Get a geometry object from the Lua stack and return a pointer to it.
 *
 * \param lua_state The lua state.
 * \param n The position in the stack, usually 1 for the first function
 *          parameter.
 */
geom::geometry_t *unpack_geometry(lua_State *lua_state, int n = 1) noexcept;

/**
 * Define the osm2pgsql.Geometry class/metatable.
 */
void init_geometry_class(lua_State *lua_state);

#endif // OSM2PGSQL_FLEX_LUA_GEOM_HPP
