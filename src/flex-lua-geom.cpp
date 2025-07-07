/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2025 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "flex-lua-geom.hpp"
#include "geom-box.hpp"
#include "geom-functions.hpp"
#include "geom-pole-of-inaccessibility.hpp"
#include "lua-utils.hpp"
#include "projection.hpp"

#include <lua.hpp>

static char const *const OSM2PGSQL_GEOMETRY_CLASS = "osm2pgsql.Geometry";

geom::geometry_t *create_lua_geometry_object(lua_State *lua_state)
{
    void *ptr = lua_newuserdata(lua_state, sizeof(geom::geometry_t));
    new (ptr) geom::geometry_t{};

    // Set the metatable of this object
    luaL_getmetatable(lua_state, OSM2PGSQL_GEOMETRY_CLASS);
    lua_setmetatable(lua_state, -2);

    return static_cast<geom::geometry_t *>(ptr);
}

geom::geometry_t *unpack_geometry(lua_State *lua_state, int n) noexcept
{
    void *user_data = luaL_checkudata(lua_state, n, OSM2PGSQL_GEOMETRY_CLASS);
    luaL_argcheck(lua_state, user_data != nullptr, n, "'Geometry' expected");
    return static_cast<geom::geometry_t *>(user_data);
}

namespace {

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

int geom_area(lua_State *lua_state)
{
    auto const *const input_geometry = unpack_geometry(lua_state);

    try {
        lua_pushnumber(lua_state, geom::area(*input_geometry));
    } catch (...) {
        return luaL_error(lua_state, "Unknown error in 'area()'.\n");
    }

    return 1;
}

int geom_spherical_area(lua_State *lua_state)
{
    auto const *const input_geometry = unpack_geometry(lua_state);

    if (input_geometry->srid() != PROJ_LATLONG) {
        throw std::runtime_error{"Can only calculate spherical area for "
                                 "geometries in WGS84 (4326) coordinates."};
    }

    try {
        lua_pushnumber(lua_state, geom::spherical_area(*input_geometry));
    } catch (...) {
        return luaL_error(lua_state, "Unknown error in 'spherical_area()'.\n");
    }

    return 1;
}

int geom_length(lua_State *lua_state)
{
    auto const *const input_geometry = unpack_geometry(lua_state);
    try {
        lua_pushnumber(lua_state, geom::length(*input_geometry));
    } catch (...) {
        return luaL_error(lua_state, "Unknown error in 'length()'.\n");
    }

    return 1;
}

int geom_centroid(lua_State *lua_state)
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

int geom_geometry_n(lua_State *lua_state)
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

int geom_geometry_type(lua_State *lua_state)
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

int geom_is_null(lua_State *lua_state)
{
    auto const *const input_geometry = unpack_geometry(lua_state);
    lua_pushboolean(lua_state, input_geometry->is_null());
    return 1;
}

int geom_reverse(lua_State *lua_state)
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

int geom_line_merge(lua_State *lua_state)
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

int geom_num_geometries(lua_State *lua_state)
{
    auto const *const input_geometry = unpack_geometry(lua_state);
    lua_pushinteger(lua_state,
                    static_cast<lua_Integer>(num_geometries(*input_geometry)));
    return 1;
}

int geom_pole_of_inaccessibility(lua_State *lua_state)
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

int geom_segmentize(lua_State *lua_state)
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

int geom_simplify(lua_State *lua_state)
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

int geom_get_bbox(lua_State *lua_state)
{
    auto const *const input_geometry = unpack_geometry(lua_state);

    try {
        auto const box = geom::envelope(*input_geometry);
        lua_pushnumber(lua_state, box.min_x());
        lua_pushnumber(lua_state, box.min_y());
        lua_pushnumber(lua_state, box.max_x());
        lua_pushnumber(lua_state, box.max_y());
    } catch (...) {
        return luaL_error(lua_state, "Unknown error in 'get_bbox()'.\n");
    }

    return 4;
}

int geom_srid(lua_State *lua_state)
{
    auto const *const input_geometry = unpack_geometry(lua_state);
    lua_pushinteger(lua_state,
                    static_cast<lua_Integer>(input_geometry->srid()));
    return 1;
}

// XXX Implementation for Lua __tostring function on geometries. Currently
// just returns the type as string. This could be improved, for instance by
// showing a WKT representation of the geometry.
int geom_tostring(lua_State *lua_state)
{
    return geom_geometry_type(lua_state);
}

int geom_transform(lua_State *lua_state)
{
    auto const *const input_geometry = unpack_geometry(lua_state);
    auto const srid = static_cast<int>(luaL_checkinteger(lua_state, 2));

    try {
        if (input_geometry->srid() != PROJ_LATLONG) {
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

} // anonymous namespace

void init_geometry_class(lua_State *lua_state)
{
    luaX_set_up_metatable(
        lua_state, "Geometry", OSM2PGSQL_GEOMETRY_CLASS,
        {{"__gc", geom_gc},
         {"__len", geom_num_geometries},
         {"__tostring", geom_tostring},
         {"area", geom_area},
         {"length", geom_length},
         {"centroid", geom_centroid},
         {"get_bbox", geom_get_bbox},
         {"geometry_n", geom_geometry_n},
         {"geometry_type", geom_geometry_type},
         {"is_null", geom_is_null},
         {"line_merge", geom_line_merge},
         {"reverse", geom_reverse},
         {"num_geometries", geom_num_geometries},
         {"pole_of_inaccessibility", geom_pole_of_inaccessibility},
         {"segmentize", geom_segmentize},
         {"simplify", geom_simplify},
         {"spherical_area", geom_spherical_area},
         {"srid", geom_srid},
         {"transform", geom_transform}});
}
