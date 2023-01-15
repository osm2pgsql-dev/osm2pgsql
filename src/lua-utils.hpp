#ifndef OSM2PGSQL_LUA_UTILS_HPP
#define OSM2PGSQL_LUA_UTILS_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

// This file contains helper functions for talking to Lua. It is used from
// the flex output backend. All functions start with "luaX_".

extern "C"
{
#include <lua.h>
}

#include <cassert>
#include <cstdint>
#include <utility>

void luaX_set_context(lua_State *lua_state, void *ptr) noexcept;
void *luaX_get_context(lua_State *lua_state) noexcept;

void luaX_add_table_str(lua_State *lua_state, char const *key,
                        char const *value) noexcept;
void luaX_add_table_str(lua_State *lua_state, char const *key,
                        char const *value, std::size_t size) noexcept;
void luaX_add_table_int(lua_State *lua_state, char const *key,
                        int64_t value) noexcept;
void luaX_add_table_num(lua_State *lua_state, char const *key,
                        double value) noexcept;
void luaX_add_table_bool(lua_State *lua_state, char const *key,
                         bool value) noexcept;
void luaX_add_table_func(lua_State *lua_state, char const *key,
                         lua_CFunction func) noexcept;

template <typename COLLECTION, typename FUNC>
void luaX_add_table_array(lua_State *lua_state, char const *key,
                          COLLECTION const &collection, FUNC &&func)
{
    lua_pushstring(lua_state, key);
    lua_createtable(lua_state, (int)collection.size(), 0);
    int n = 0;
    for (auto const &member : collection) {
        lua_pushinteger(lua_state, ++n);
        std::forward<FUNC>(func)(member);
        lua_rawset(lua_state, -3);
    }
    lua_rawset(lua_state, -3);
}

char const *luaX_get_table_string(lua_State *lua_state, char const *key,
                                  int table_index, char const *error_msg);

char const *luaX_get_table_string(lua_State *lua_state, char const *key,
                                  int table_index, char const *error_msg,
                                  char const *default_value);

bool luaX_get_table_bool(lua_State *lua_state, char const *key, int table_index,
                         char const *error_msg, bool default_value);

int luaX_pcall(lua_State *lua_state, int narg, int nres);

/**
 * Returns true if the value on top of the stack is an empty Lua table.
 *
 * \pre Value on top of the Lua stack must be a Lua table.
 * \post Stack is unchanged.
 */
bool luaX_is_empty_table(lua_State *lua_state);

/**
 * Check that the value on the top of the Lua stack is a simple array.
 * This means that all keys must be consecutive integers starting from 1.
 *
 * \returns True if this is an array (also for Lua tables without any items)
 *
 * \pre Value on top of the Lua stack must be a Lua table.
 * \post Stack is unchanged.
 */
bool luaX_is_array(lua_State *lua_state);

/**
 * Call a function for each item in a Lua array table. The item value will
 * be on the top of the stack inside that function.
 *
 * \pre Value on top of the Lua stack must be a Lua array table.
 * \pre The function must leave the Lua stack in the same condition it found
 *      it in.
 * \post Stack is unchanged.
 */
template <typename FUNC>
void luaX_for_each(lua_State *lua_state, FUNC &&func)
{
    assert(lua_istable(lua_state, -1));
    lua_pushnil(lua_state);
    while (lua_next(lua_state, -2) != 0) {
#ifndef NDEBUG
        int const top = lua_gettop(lua_state);
#endif
        std::forward<FUNC>(func)();
        assert(top == lua_gettop(lua_state));
        lua_pop(lua_state, 1);
    }
}

#endif // OSM2PGSQL_LUA_UTILS_HPP
