#ifndef OSM2PGSQL_FLEX_LUA_WRAPPER_HPP
#define OSM2PGSQL_FLEX_LUA_WRAPPER_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2025 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "output-flex.hpp"

#include <cassert>
#include <exception>

#define TRAMPOLINE_WRAPPED_OBJECT(obj_name, func_name)                         \
    int lua_trampoline_##obj_name##_##func_name(lua_State *lua_state)          \
    {                                                                          \
        try {                                                                  \
            auto *flex =                                                       \
                static_cast<output_flex_t *>(luaX_get_context(lua_state));     \
            auto &obj = flex->get_##obj_name##_from_param();                   \
            return lua_wrapper_##obj_name{lua_state, &obj}.func_name();        \
        } catch (std::exception const &e) {                                    \
            return luaL_error(lua_state, "Error in '" #func_name "': %s\n",    \
                              e.what());                                       \
        } catch (...) {                                                        \
            return luaL_error(lua_state,                                       \
                              "Unknown error in '" #func_name "'.\n");         \
        }                                                                      \
    }

struct lua_State;

/**
 * Helper class for wrapping C++ classes in Lua "classes".
 */
template <typename WRAPPED>
class lua_wrapper_base
{
public:
    lua_wrapper_base(lua_State *lua_state, WRAPPED *wrapped)
    : m_lua_state(lua_state), m_self(wrapped)
    {
        assert(lua_state);
        assert(wrapped);
    }

protected:
    lua_State *lua_state() const noexcept { return m_lua_state; }

    WRAPPED const &self() const noexcept { return *m_self; }
    WRAPPED &self() noexcept { return *m_self; }

private:
    lua_State *m_lua_state;
    WRAPPED *m_self;

}; // class lua_wrapper_base;

#endif // OSM2PGSQL_FLEX_LUA_WRAPPER_HPP
