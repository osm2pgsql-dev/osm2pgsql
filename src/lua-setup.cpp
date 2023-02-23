/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "lua-setup.hpp"
#include "lua-utils.hpp"
#include "version.hpp"

#include <lua.hpp>

#include <boost/filesystem.hpp>

void setup_lua_environment(lua_State *lua_state, std::string const &filename,
                           bool append_mode)
{
    // Set up global lua libs
    luaL_openlibs(lua_state);

    // Set up global "osm2pgsql" object
    lua_newtable(lua_state);
    lua_pushvalue(lua_state, -1);
    lua_setglobal(lua_state, "osm2pgsql");

    luaX_add_table_str(lua_state, "version", get_osm2pgsql_short_version());

    std::string dir_path =
        boost::filesystem::path{filename}.parent_path().string();
    if (!dir_path.empty()) {
        dir_path += boost::filesystem::path::preferred_separator;
    }
    luaX_add_table_str(lua_state, "config_dir", dir_path.c_str());

    luaX_add_table_str(lua_state, "mode", append_mode ? "append" : "create");
}
