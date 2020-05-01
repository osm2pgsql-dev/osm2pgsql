#ifndef OSM2PGSQL_FLEX_LUA_HPP
#define OSM2PGSQL_FLEX_LUA_HPP

// This file contains helper functions for talking to Lua. It is used from
// the flex output backend. All functions start with "luaX_".

extern "C"
{
#include <lua.h>
}

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

bool luaX_get_table_bool(lua_State *lua_state, char const *key, int table_index,
                         char const *error_msg, bool default_value);

int luaX_pcall(lua_State *lua_state, int narg, int nres);

#endif // OSM2PGSQL_FLEX_LUA_HPP
