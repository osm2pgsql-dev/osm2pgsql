#include "config.h"
#include "lua-utils.hpp"
#include "format.hpp"

extern "C"
{
#include <lauxlib.h>
}

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

// Unique key for lua registry
static char const *osm2pgsql_output_flex = "osm2pgsql_output_flex";

void luaX_set_context(lua_State *lua_state, void *ptr) noexcept
{
    assert(lua_state);
    assert(ptr);
    lua_pushlightuserdata(lua_state, (void *)osm2pgsql_output_flex);
    lua_pushlightuserdata(lua_state, ptr);
    lua_settable(lua_state, LUA_REGISTRYINDEX);
}

void *luaX_get_context(lua_State *lua_state) noexcept
{
    assert(lua_state);
    lua_pushlightuserdata(lua_state, (void *)osm2pgsql_output_flex);
    lua_gettable(lua_state, LUA_REGISTRYINDEX);
    auto *const ptr = lua_touserdata(lua_state, -1);
    assert(ptr);
    lua_pop(lua_state, 1);
    return ptr;
}

#endif

void luaX_add_table_str(lua_State *lua_state, char const *key,
                        char const *value) noexcept
{
    lua_pushstring(lua_state, key);
    lua_pushstring(lua_state, value);
    lua_rawset(lua_state, -3);
}

void luaX_add_table_str(lua_State *lua_state, char const *key,
                        char const *value, std::size_t size) noexcept
{
    lua_pushstring(lua_state, key);
    lua_pushlstring(lua_state, value, size);
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

char const *luaX_get_table_string(lua_State *lua_state, char const *key,
                                  int table_index, char const *error_msg)
{
    assert(lua_state);
    assert(key);
    assert(error_msg);
    lua_getfield(lua_state, table_index, key);
    if (!lua_isstring(lua_state, -1)) {
        throw std::runtime_error{
            "{} must contain a '{}' string field."_format(error_msg, key)};
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

    throw std::runtime_error{
        "{} must contain a '{}' boolean field."_format(error_msg, key)};
}


// Lua 5.1 doesn't support luaL_traceback, unless LuaJIT is used
#if LUA_VERSION_NUM < 502 && !defined(HAVE_LUAJIT)

int luaX_pcall(lua_State *lua_state, int narg, int nres)
{
    return lua_pcall(lua_state, narg, nres, 0);
}

#else

static int pcall_error_traceback_handler(lua_State *lua_state)
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
