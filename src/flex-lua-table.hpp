#ifndef OSM2PGSQL_FLEX_LUA_TABLE_HPP
#define OSM2PGSQL_FLEX_LUA_TABLE_HPP

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
class flex_table_t;
struct lua_State;

static char const *const OSM2PGSQL_TABLE_CLASS = "osm2pgsql.Table";

int setup_flex_table(lua_State *lua_state, std::vector<flex_table_t> *tables,
                     std::vector<expire_output_t> *expire_outputs,
                     std::string const &default_schema, bool updatable,
                     bool append_mode);

class lua_wrapper_table_t : public lua_wrapper_base_t<flex_table_t>
{
public:
    static void init(lua_State *lua_state);

    lua_wrapper_table_t(lua_State *lua_state, flex_table_t *table)
    : lua_wrapper_base_t(lua_state, table)
    {
    }

    int tostring() const;
    int cluster() const noexcept;
    int columns() const;
    int name() const noexcept;
    int schema() const noexcept;

}; // class lua_wrapper_table_t

#endif // OSM2PGSQL_FLEX_LUA_TABLE_HPP
