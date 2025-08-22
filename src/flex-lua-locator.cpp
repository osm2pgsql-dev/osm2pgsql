/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2025 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "flex-lua-locator.hpp"

#include "flex-lua-geom.hpp"
#include "flex-lua-wrapper.hpp"
#include "lua-utils.hpp"
#include "pgsql.hpp"

#include <stdexcept>
#include <string>
#include <vector>

#include <lua.hpp>

namespace {

locator_t &create_locator(lua_State *lua_state,
                          std::vector<locator_t> *locators)
{
    auto &new_locator = locators->emplace_back();

    // optional "name" field
    auto const *name =
        luaX_get_table_string(lua_state, "name", -1, "The locator", "");
    new_locator.set_name(name);
    lua_pop(lua_state, 1); // "name"

    return new_locator;
}

TRAMPOLINE_WRAPPED_OBJECT(locator, tostring)
TRAMPOLINE_WRAPPED_OBJECT(locator, name)
TRAMPOLINE_WRAPPED_OBJECT(locator, add_bbox)
TRAMPOLINE_WRAPPED_OBJECT(locator, add_from_db)
TRAMPOLINE_WRAPPED_OBJECT(locator, all_intersecting)
TRAMPOLINE_WRAPPED_OBJECT(locator, first_intersecting)

} // anonymous namespace

connection_params_t lua_wrapper_locator_t::s_connection_params;

int setup_flex_locator(lua_State *lua_state, std::vector<locator_t> *locators)
{
    if (lua_type(lua_state, 1) != LUA_TTABLE) {
        throw std::runtime_error{
            "Argument #1 to 'define_locator' must be a Lua table."};
    }

    create_locator(lua_state, locators);

    void *ptr = lua_newuserdata(lua_state, sizeof(std::size_t));
    auto *num = new (ptr) std::size_t{};
    *num = locators->size() - 1;
    luaL_getmetatable(lua_state, OSM2PGSQL_LOCATOR_CLASS);
    lua_setmetatable(lua_state, -2);

    return 1;
}

void lua_wrapper_locator_t::init(lua_State *lua_state,
                                 connection_params_t const &connection_params)
{
    s_connection_params = connection_params;

    luaX_set_up_metatable(
        lua_state, "Locator", OSM2PGSQL_LOCATOR_CLASS,
        {{"__tostring", lua_trampoline_locator_tostring},
         {"name", lua_trampoline_locator_name},
         {"add_bbox", lua_trampoline_locator_add_bbox},
         {"add_from_db", lua_trampoline_locator_add_from_db},
         {"all_intersecting", lua_trampoline_locator_all_intersecting},
         {"first_intersecting", lua_trampoline_locator_first_intersecting}});
}

int lua_wrapper_locator_t::tostring() const
{
    std::string const str{fmt::format("osm2pgsql.Locator[name={},size={}]",
                                      self().name(), self().size())};
    luaX_pushstring(lua_state(), str);
    return 1;
}

int lua_wrapper_locator_t::name() const noexcept
{
    luaX_pushstring(lua_state(), self().name());
    return 1;
}

int lua_wrapper_locator_t::add_bbox()
{
    if (lua_gettop(lua_state()) < 5) {
        throw fmt_error("Need locator, name and 4 coordinates as arguments");
    }

    std::string const name = lua_tostring(lua_state(), 1);
    double const min_x = lua_tonumber(lua_state(), 2);
    double const min_y = lua_tonumber(lua_state(), 3);
    double const max_x = lua_tonumber(lua_state(), 4);
    double const max_y = lua_tonumber(lua_state(), 5);

    self().add_region(name, geom::box_t{min_x, min_y, max_x, max_y});

    return 0;
}

int lua_wrapper_locator_t::add_from_db()
{
    if (lua_gettop(lua_state()) < 1) {
        throw fmt_error("Need locator and SQL query arguments");
    }

    std::string const query = lua_tostring(lua_state(), 1);

    pg_conn_t const db_connection{s_connection_params, "flex.locator"};
    self().add_regions(db_connection, query);

    return 0;
}

int lua_wrapper_locator_t::all_intersecting()
{
    if (lua_gettop(lua_state()) < 1) {
        throw fmt_error("Need locator and geometry arguments");
    }

    auto const *geometry = unpack_geometry(lua_state());

    if (!geometry) {
        throw fmt_error("Second argument must be a geometry");
    }

    auto const names = self().all_intersecting(*geometry);
    lua_createtable(lua_state(), (int)names.size(), 0);
    int n = 0;
    for (auto const &name : names) {
        lua_pushinteger(lua_state(), ++n);
        luaX_pushstring(lua_state(), name);
        lua_rawset(lua_state(), -3);
    }

    return 1;
}

int lua_wrapper_locator_t::first_intersecting()
{
    if (lua_gettop(lua_state()) < 1) {
        throw fmt_error("Need locator and geometry arguments");
    }

    auto const *geometry = unpack_geometry(lua_state());

    if (!geometry) {
        throw fmt_error("Second argument must be a geometry");
    }

    auto const name = self().first_intersecting(*geometry);
    if (name.empty()) {
        return 0;
    }

    luaX_pushstring(lua_state(), name);

    return 1;
}
