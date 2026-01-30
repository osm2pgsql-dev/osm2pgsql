#ifndef OSM2PGSQL_FLEX_LUA_EXPIRE_OUTPUT_HPP
#define OSM2PGSQL_FLEX_LUA_EXPIRE_OUTPUT_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2026 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "flex-lua-wrapper.hpp"

#include <string>
#include <vector>

class expire_output_t;
struct lua_State;

static char const *const OSM2PGSQL_EXPIRE_OUTPUT_CLASS =
    "osm2pgsql.ExpireOutput";

int setup_flex_expire_output(lua_State *lua_state,
                             std::string const &default_schema,
                             std::vector<expire_output_t> *expire_outputs);

class lua_wrapper_expire_output_t : public lua_wrapper_base_t<expire_output_t>
{
public:
    static void init(lua_State *lua_state);

    lua_wrapper_expire_output_t(lua_State *lua_state,
                                expire_output_t *expire_output)
    : lua_wrapper_base_t(lua_state, expire_output)
    {
    }

    int tostring() const;
    int filename() const noexcept;
    int maxzoom() const noexcept;
    int minzoom() const noexcept;
    int schema() const noexcept;
    int table() const noexcept;
    int max_tiles_geometry() const noexcept;
    int max_tiles_overall() const noexcept;

}; // class lua_wrapper_expire_output_t

#endif // OSM2PGSQL_FLEX_LUA_EXPIRE_OUTPUT_HPP
