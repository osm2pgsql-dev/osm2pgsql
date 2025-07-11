/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2025 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "lua-utils.hpp"

#include "format.hpp"

#include <cassert>

// The lua_getextraspace() function is only available from Lua 5.3. For
// earlier versions we fall back to storing the context pointer in the
// Lua registry which is somewhat more effort so will be slower.
#if LUA_VERSION_NUM >= 503

void luaX_set_context(lua_State *lua_state, void *ptr) noexcept
{
    assert(lua_state);
    assert(ptr);
    *static_cast<void **>(lua_getextraspace(lua_state)) = ptr;
}

void *luaX_get_context(lua_State *lua_state) noexcept
{
    assert(lua_state);
    return *static_cast<void **>(lua_getextraspace(lua_state));
}

#else

namespace {

// Unique key for lua registry
constexpr char const *const OSM2PGSQL_OUTPUT_FLEX = "osm2pgsql_output_flex";

} // anonymous namespace

void luaX_set_context(lua_State *lua_state, void *ptr) noexcept
{
    assert(lua_state);
    assert(ptr);
    lua_pushlightuserdata(lua_state, (void *)OSM2PGSQL_OUTPUT_FLEX);
    lua_pushlightuserdata(lua_state, ptr);
    lua_settable(lua_state, LUA_REGISTRYINDEX);
}

void *luaX_get_context(lua_State *lua_state) noexcept
{
    assert(lua_state);
    lua_pushlightuserdata(lua_state, (void *)OSM2PGSQL_OUTPUT_FLEX);
    lua_gettable(lua_state, LUA_REGISTRYINDEX);
    auto *const ptr = lua_touserdata(lua_state, -1);
    assert(ptr);
    lua_pop(lua_state, 1);
    return ptr;
}

#endif

void luaX_pushstring(lua_State *lua_state, std::string_view str) noexcept
{
    lua_pushlstring(lua_state, str.data(), str.size());
}

void luaX_add_table_str(lua_State *lua_state, char const *key,
                        char const *value) noexcept
{
    lua_pushstring(lua_state, key);
    lua_pushstring(lua_state, value);
    lua_rawset(lua_state, -3);
}

void luaX_add_table_str(lua_State *lua_state, char const *key,
                        std::string_view value) noexcept
{
    lua_pushstring(lua_state, key);
    luaX_pushstring(lua_state, value);
    lua_rawset(lua_state, -3);
}

void luaX_add_table_int(lua_State *lua_state, char const *key,
                        int64_t value) noexcept
{
    lua_pushstring(lua_state, key);
    lua_pushinteger(lua_state, value);
    lua_rawset(lua_state, -3);
}

void luaX_add_table_num(lua_State *lua_state, char const *key,
                        double value) noexcept
{
    lua_pushstring(lua_state, key);
    lua_pushnumber(lua_state, value);
    lua_rawset(lua_state, -3);
}

void luaX_add_table_bool(lua_State *lua_state, char const *key,
                         bool value) noexcept
{
    lua_pushstring(lua_state, key);
    lua_pushboolean(lua_state, value);
    lua_rawset(lua_state, -3);
}

void luaX_add_table_func(lua_State *lua_state, char const *key,
                         lua_CFunction func) noexcept
{
    lua_pushstring(lua_state, key);
    lua_pushcfunction(lua_state, func);
    lua_rawset(lua_state, -3);
}

void luaX_set_up_metatable(
    lua_State *lua_state, char const *name, char const *luaclass,
    std::initializer_list<std::pair<char const *, lua_CFunction>> map)

{
    lua_getglobal(lua_state, "osm2pgsql");
    if (luaL_newmetatable(lua_state, luaclass) != 1) {
        throw std::runtime_error{"Internal error: Lua newmetatable failed."};
    }
    lua_pushvalue(lua_state, -1); // Copy of new metatable

    // Add metatable under the specified name so we can access it from Lua
    lua_setfield(lua_state, -3, name);

    // Now add functions to metatable
    lua_pushvalue(lua_state, -1);
    lua_setfield(lua_state, -2, "__index");
    for (auto const &[key, func] : map) {
        lua_pushstring(lua_state, key);
        lua_pushcfunction(lua_state, func);
        lua_rawset(lua_state, -3);
    }

    lua_settop(lua_state, 0);
}

char const *luaX_get_table_string(lua_State *lua_state, char const *key,
                                  int table_index, char const *error_msg)
{
    assert(lua_state);
    assert(key);
    assert(error_msg);
    lua_getfield(lua_state, table_index, key);
    if (!lua_isstring(lua_state, -1)) {
        throw fmt_error("{} must contain a '{}' string field.", error_msg, key);
    }
    return lua_tostring(lua_state, -1);
}

char const *luaX_get_table_string(lua_State *lua_state, char const *key,
                                  int table_index, char const *error_msg,
                                  char const *default_value)
{
    assert(lua_state);
    assert(key);
    lua_getfield(lua_state, table_index, key);
    int const ltype = lua_type(lua_state, -1);
    if (ltype == LUA_TNIL) {
        return default_value;
    }
    if (ltype != LUA_TSTRING) {
        throw fmt_error("{} field must contain a '{}' string field "
                        "(or nil for default: '{}').",
                        error_msg, key, default_value);
    }
    return lua_tostring(lua_state, -1);
}

bool luaX_get_table_bool(lua_State *lua_state, char const *key, int table_index,
                         char const *error_msg, bool default_value)
{
    assert(lua_state);
    assert(key);
    assert(error_msg);
    lua_getfield(lua_state, table_index, key);
    auto const type = lua_type(lua_state, -1);

    if (type == LUA_TNIL) {
        return default_value;
    }

    if (type == LUA_TBOOLEAN) {
        return lua_toboolean(lua_state, -1);
    }

    throw fmt_error("{} field '{}' must be a boolean field.", error_msg, key);
}

uint32_t luaX_get_table_optional_uint32(lua_State *lua_state, char const *key,
                                        int table_index, char const *error_msg,
                                        uint32_t min, uint32_t max,
                                        char const *range)
{
    assert(lua_state);
    assert(key);
    assert(error_msg);
    lua_getfield(lua_state, table_index, key);
    if (lua_isnil(lua_state, -1)) {
        return 0;
    }
    if (!lua_isnumber(lua_state, -1)) {
        throw fmt_error("{} must contain an integer.", error_msg);
    }

    auto const num = lua_tonumber(lua_state, -1);
    if (num < static_cast<double>(min) || num > static_cast<double>(max)) {
        throw fmt_error("{} must be between {}.", error_msg, range);
    }

    return static_cast<uint32_t>(num);
}

// Lua 5.1 doesn't support luaL_traceback, unless LuaJIT is used
#if LUA_VERSION_NUM < 502 && !defined(HAVE_LUAJIT)

int luaX_pcall(lua_State *lua_state, int narg, int nres)
{
    return lua_pcall(lua_state, narg, nres, 0);
}

#else

namespace {

int pcall_error_traceback_handler(lua_State *lua_state)
{
    assert(lua_state);

    char const *msg = lua_tostring(lua_state, 1);
    if (msg == nullptr) {
        if (luaL_callmeta(lua_state, 1, "__tostring") &&
            lua_type(lua_state, -1) == LUA_TSTRING) {
            return 1;
        }
        msg = lua_pushfstring(lua_state, "(error object is a %s value)",
                              luaL_typename(lua_state, 1));
    }
    luaL_traceback(lua_state, lua_state, msg, 1);
    return 1;
}

} // anonymous namespace

/// Wrapper function for lua_pcall() showing a stack trace on error.
int luaX_pcall(lua_State *lua_state, int narg, int nres)
{
    int const base = lua_gettop(lua_state) - narg;
    lua_pushcfunction(lua_state, pcall_error_traceback_handler);
    lua_insert(lua_state, base);
    int const status = lua_pcall(lua_state, narg, nres, base);
    lua_remove(lua_state, base);
    return status;
}

#endif

bool luaX_is_empty_table(lua_State *lua_state)
{
    assert(lua_istable(lua_state, -1));
    lua_pushnil(lua_state);
    if (lua_next(lua_state, -2) == 0) {
        return true;
    }
    lua_pop(lua_state, 2);
    return false;
}

bool luaX_is_array(lua_State *lua_state)
{
    // Checking that a Lua table is an array is surprisingly difficult.
    // This code is based on:
    // https://web.archive.org/web/20140227143701/http://ericjmritz.name/2014/02/26/lua-is_array/
    assert(lua_istable(lua_state, -1));
    int i = 0;
    lua_pushnil(lua_state);
    while (lua_next(lua_state, -2) != 0) {
        ++i;
        lua_rawgeti(lua_state, -3, i);
        if (lua_isnil(lua_state, -1)) {
            lua_pop(lua_state, 3);
            return false;
        }
        lua_pop(lua_state, 2);
    }
    return true;
}
