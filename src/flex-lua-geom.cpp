/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "flex-lua-geom.hpp"
#include "geom-functions.hpp"
#include "geom-pole-of-inaccessibility.hpp"
#include "lua-utils.hpp"

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
}

static char const *const osm2pgsql_geometry_class = "osm2pgsql.Geometry";

geom::geometry_t *create_lua_geometry_object(lua_State *lua_state)
{
    void *ptr = lua_newuserdata(lua_state, sizeof(geom::geometry_t));
    new (ptr) geom::geometry_t{};

    // Set the metatable of this object
    luaL_getmetatable(lua_state, osm2pgsql_geometry_class);
    lua_setmetatable(lua_state, -2);

    return static_cast<geom::geometry_t *>(ptr);
}

geom::geometry_t *unpack_geometry(lua_State *lua_state, int n) noexcept
{
    void *user_data = luaL_checkudata(lua_state, n, osm2pgsql_geometry_class);
    luaL_argcheck(lua_state, user_data != nullptr, n, "'Geometry' expected");
    return static_cast<geom::geometry_t *>(user_data);
}

/**
 * This function is called by Lua garbage collection when a geometry object
 * needs cleaning up. It calls the destructor of the C++ object. After that
 * Lua will release the memory.
 */
int geom_gc(lua_State *lua_state) noexcept
{
    void *geom = lua_touserdata(lua_state, 1);
    if (geom) {
        static_cast<geom::geometry_t *>(geom)->~geometry_t();
    }

    return 0;
}

// The following functions are called when their counterparts in Lua are
// called on geometry objects.

static int geom_area(lua_State *lua_state)
{
    auto const *const input_geometry = unpack_geometry(lua_state);

    try {
        lua_pushnumber(lua_state, geom::area(*input_geometry));
    } catch (...) {
        return luaL_error(lua_state, "Unknown error in 'area()'.\n");
    }

    return 1;
}

static int geom_length(lua_State *lua_state)
{
    auto const *const input_geometry = unpack_geometry(lua_state);
    try {
        lua_pushnumber(lua_state, geom::length(*input_geometry));
    } catch (...) {
        return luaL_error(lua_state, "Unknown error in 'length()'.\n");
    }

    return 1;
}

static int geom_centroid(lua_State *lua_state)
{
    auto const *const input_geometry = unpack_geometry(lua_state);

    try {
        auto *geom = create_lua_geometry_object(lua_state);
        *geom = geom::centroid(*input_geometry);
    } catch (...) {
        return luaL_error(lua_state, "Unknown error in 'centroid()'.\n");
    }

    return 1;
}

static int geom_geometry_n(lua_State *lua_state)
{
    auto const *const input_geometry = unpack_geometry(lua_state);
    auto const index = static_cast<int>(luaL_checkinteger(lua_state, 2));

    try {
        auto *geom = create_lua_geometry_object(lua_state);
        geom::geometry_n(geom, *input_geometry, index);
    } catch (...) {
        return luaL_error(lua_state, "Unknown error in 'geometry_n()'.\n");
    }

    return 1;
}

static int geom_geometry_type(lua_State *lua_state)
{
    auto const *const input_geometry = unpack_geometry(lua_state);

    try {
        auto const type = geometry_type(*input_geometry);
        lua_pushlstring(lua_state, type.data(), type.size());
    } catch (...) {
        return luaL_error(lua_state, "Unknown error in 'geometry_type()'.\n");
    }

    return 1;
}

static int geom_is_null(lua_State *lua_state)
{
    auto const *const input_geometry = unpack_geometry(lua_state);
    lua_pushboolean(lua_state, input_geometry->is_null());
    return 1;
}

static int geom_reverse(lua_State *lua_state)
{
    auto const *const input_geometry = unpack_geometry(lua_state);

    try {
        auto *geom = create_lua_geometry_object(lua_state);
        geom::reverse(geom, *input_geometry);
    } catch (...) {
        return luaL_error(lua_state, "Unknown error in 'reverse()'.\n");
    }

    return 1;
}

static int geom_line_merge(lua_State *lua_state)
{
    auto const *const input_geometry = unpack_geometry(lua_state);

    try {
        auto *geom = create_lua_geometry_object(lua_state);
        geom::line_merge(geom, *input_geometry);
    } catch (...) {
        return luaL_error(lua_state, "Unknown error in 'line_merge()'.\n");
    }

    return 1;
}

static int geom_num_geometries(lua_State *lua_state)
{
    auto const *const input_geometry = unpack_geometry(lua_state);
    lua_pushinteger(lua_state,
                    static_cast<lua_Integer>(num_geometries(*input_geometry)));
    return 1;
}

static int geom_pole_of_inaccessibility(lua_State *lua_state)
{
    auto const *const input_geometry = unpack_geometry(lua_state);

    double stretch = 1.0;
    if (lua_gettop(lua_state) > 1) {
        if (lua_type(lua_state, 2) != LUA_TTABLE) {
            throw std::runtime_error{
                "Argument #2 to 'pole_of_inaccessibility' must be a table."};
        }

        lua_getfield(lua_state, 2, "stretch");
        if (lua_isnumber(lua_state, -1)) {
            stretch = lua_tonumber(lua_state, -1);
            if (stretch <= 0.0) {
                throw std::runtime_error{"The 'stretch' factor must be > 0."};
            }
        } else {
            throw std::runtime_error{"The 'stretch' factor must be a number."};
        }
    }

    auto *geom = create_lua_geometry_object(lua_state);
    geom::pole_of_inaccessibility(geom, *input_geometry, 0, stretch);

    return 1;
}

static int geom_segmentize(lua_State *lua_state)
{
    auto const *const input_geometry = unpack_geometry(lua_state);
    double const max_segment_length = luaL_checknumber(lua_state, 2);

    try {
        auto *geom = create_lua_geometry_object(lua_state);
        geom::segmentize(geom, *input_geometry, max_segment_length);
    } catch (...) {
        return luaL_error(lua_state, "Unknown error in 'segmentize()'.\n");
    }

    return 1;
}

static int geom_simplify(lua_State *lua_state)
{
    auto const *const input_geometry = unpack_geometry(lua_state);
    double const tolerance = luaL_checknumber(lua_state, 2);

    try {
        auto *geom = create_lua_geometry_object(lua_state);
        geom::simplify(geom, *input_geometry, tolerance);
    } catch (...) {
        return luaL_error(lua_state, "Unknown error in 'simplify()'.\n");
    }

    return 1;
}

static int geom_srid(lua_State *lua_state)
{
    auto const *const input_geometry = unpack_geometry(lua_state);
    lua_pushinteger(lua_state,
                    static_cast<lua_Integer>(input_geometry->srid()));
    return 1;
}

// XXX Implementation for Lua __tostring function on geometries. Currently
// just returns the type as string. This could be improved, for instance by
// showing a WKT representation of the geometry.
static int geom_tostring(lua_State *lua_state)
{
    return geom_geometry_type(lua_state);
}

static int geom_transform(lua_State *lua_state)
{
    auto const *const input_geometry = unpack_geometry(lua_state);
    auto const srid = static_cast<int>(luaL_checkinteger(lua_state, 2));

    try {
        if (input_geometry->srid() != 4326) {
            throw std::runtime_error{
                "Can not transform already transformed geometry."};
        }
        auto *geom = create_lua_geometry_object(lua_state);
        geom::transform(geom, *input_geometry, get_projection(srid));
    } catch (...) {
        return luaL_error(lua_state, "Unknown error in 'transform()'.\n");
    }

    return 1;
}

void init_geometry_class(lua_State *lua_state)
{
    lua_getglobal(lua_state, "osm2pgsql");
    if (luaL_newmetatable(lua_state, osm2pgsql_geometry_class) != 1) {
        throw std::runtime_error{"Internal error: Lua newmetatable failed."};
    }
    lua_pushvalue(lua_state, -1); // Copy of new metatable

    // Add metatable as osm2pgsql.Geometry so we can access it from Lua
    lua_setfield(lua_state, -3, "Geometry");

    luaX_add_table_func(lua_state, "__gc", geom_gc);
    luaX_add_table_func(lua_state, "__len", geom_num_geometries);
    luaX_add_table_func(lua_state, "__tostring", geom_tostring);
    lua_pushvalue(lua_state, -1);
    lua_setfield(lua_state, -2, "__index");
    luaX_add_table_func(lua_state, "area", geom_area);
    luaX_add_table_func(lua_state, "length", geom_length);
    luaX_add_table_func(lua_state, "centroid", geom_centroid);
    luaX_add_table_func(lua_state, "geometry_n", geom_geometry_n);
    luaX_add_table_func(lua_state, "geometry_type", geom_geometry_type);
    luaX_add_table_func(lua_state, "is_null", geom_is_null);
    luaX_add_table_func(lua_state, "line_merge", geom_line_merge);
    luaX_add_table_func(lua_state, "reverse", geom_reverse);
    luaX_add_table_func(lua_state, "num_geometries", geom_num_geometries);
    luaX_add_table_func(lua_state, "pole_of_inaccessibility",
                        geom_pole_of_inaccessibility);
    luaX_add_table_func(lua_state, "segmentize", geom_segmentize);
    luaX_add_table_func(lua_state, "simplify", geom_simplify);
    luaX_add_table_func(lua_state, "srid", geom_srid);
    luaX_add_table_func(lua_state, "transform", geom_transform);

    lua_pop(lua_state, 2); // __index, global osmp2gsql
}
