#ifndef OSM2PGSQL_FLEX_LUA_LOCATOR_HPP
#define OSM2PGSQL_FLEX_LUA_LOCATOR_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2025 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

/**
 * \file
 *
 * Functions implementing the Lua interface for the locator.
 */

#include "flex-lua-wrapper.hpp"
#include "locator.hpp"

class pg_conn_t;

struct lua_State;

static char const *const osm2pgsql_locator_name = "osm2pgsql.Locator";

int setup_flex_locator(lua_State *lua_state, std::vector<locator_t> *locators);

class lua_wrapper_locator : public lua_wrapper_base<locator_t>
{
public:
    static void init(lua_State *lua_state,
                     connection_params_t const &connection_params);

    lua_wrapper_locator(lua_State *lua_state, locator_t *locator)
    : lua_wrapper_base(lua_state, locator)
    {
    }

    int tostring() const;
    int name() const;
    int add_bbox();
    int add_from_db();
    int all_intersecting();
    int first_intersecting();

private:
    static connection_params_t s_connection_params;

}; // class lua_wrapper_locator

#endif // OSM2PGSQL_FLEX_LUA_LOCATOR_HPP
