/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "geom-from-osm.hpp"
#include "geom-functions.hpp"
#include "geom-transform.hpp"
#include "logging.hpp"

#include <osmium/osm.hpp>

#include <cstring>
#include <stdexcept>

bool geom_transform_point_t::is_compatible_with(
    table_column_type geom_type) const noexcept
{
    return geom_type == table_column_type::point ||
           geom_type == table_column_type::geometry;
}

geom::geometry_t geom_transform_point_t::convert(reprojection const &proj,
                                                 osmium::Node const &node) const
{
    return geom::transform(geom::create_point(node), proj);
}

bool geom_transform_line_t::set_param(char const *name, lua_State *lua_state)
{
    if (std::strcmp(name, "split_at") != 0) {
        return false;
    }

    if (lua_type(lua_state, -1) != LUA_TNUMBER) {
        throw std::runtime_error{
            "The 'split_at' field in a geometry transformation "
            "description must be a number."};
    }
    m_split_at = lua_tonumber(lua_state, -1);

    return true;
}

bool geom_transform_line_t::is_compatible_with(
    table_column_type geom_type) const noexcept
{
    return geom_type == table_column_type::linestring ||
           geom_type == table_column_type::multilinestring ||
           geom_type == table_column_type::geometry;
}

geom::geometry_t geom_transform_line_t::convert(reprojection const &proj,
                                                osmium::Way const &way) const
{
    auto geom = geom::transform(geom::create_linestring(way), proj);
    if (!geom.is_null() && m_split_at > 0.0) {
        geom = geom::segmentize(geom, m_split_at);
    }
    return geom;
}

geom::geometry_t
geom_transform_line_t::convert(reprojection const &proj,
                               osmium::Relation const & /*relation*/,
                               osmium::memory::Buffer const &buffer) const
{
    auto geom = geom::transform(
        geom::line_merge(geom::create_multilinestring(buffer)), proj);
    if (!geom.is_null() && m_split_at > 0.0) {
        geom = geom::segmentize(geom, m_split_at);
    }
    return geom;
}

bool geom_transform_area_t::set_param(char const *name, lua_State *lua_state)
{
    if (std::strcmp(name, "multi") == 0) {
        throw std::runtime_error{
            "The 'multi' field in the geometry transformation has been"
            " removed. See docs on how to use 'split_at' instead."};
    }

    if (std::strcmp(name, "split_at") != 0) {
        return false;
    }

    char const *const val = lua_tostring(lua_state, -1);

    if (!val) {
        throw std::runtime_error{
            "The 'split_at' field in a geometry transformation "
            "description must be a string."};
    }

    if (std::strcmp(val, "multi") == 0) {
        m_multi = false;
        return true;
    }

    throw std::runtime_error{"Unknown value for 'split_at' field in a geometry"
                             " transformation: '{}'"_format(val)};
}

bool geom_transform_area_t::is_compatible_with(
    table_column_type geom_type) const noexcept
{
    return geom_type == table_column_type::polygon ||
           geom_type == table_column_type::multipolygon ||
           geom_type == table_column_type::geometry;
}

geom::geometry_t geom_transform_area_t::convert(reprojection const & /*proj*/,
                                                osmium::Way const &way) const
{
    return geom::create_polygon(way);
}

geom::geometry_t
geom_transform_area_t::convert(reprojection const & /*proj*/,
                               osmium::Relation const &relation,
                               osmium::memory::Buffer const &buffer) const
{
    return geom::create_multipolygon(relation, buffer);
}

std::unique_ptr<geom_transform_t> create_geom_transform(char const *type)
{
    if (std::strcmp(type, "point") == 0) {
        return std::make_unique<geom_transform_point_t>();
    }

    if (std::strcmp(type, "line") == 0) {
        return std::make_unique<geom_transform_line_t>();
    }

    if (std::strcmp(type, "area") == 0) {
        return std::make_unique<geom_transform_area_t>();
    }

    throw std::runtime_error{
        "Unknown geometry transformation '{}'."_format(type)};
}

void init_geom_transform(geom_transform_t *transform, lua_State *lua_state)
{
    static bool show_warning = true;

    assert(transform);
    assert(lua_state);

    lua_pushnil(lua_state);
    while (lua_next(lua_state, -2) != 0) {
        char const *const field = lua_tostring(lua_state, -2);
        if (field == nullptr) {
            throw std::runtime_error{"All fields in geometry transformation "
                                     "description must have string keys."};
        }

        if (std::strcmp(field, "create") != 0) {
            if (!transform->set_param(field, lua_state) && show_warning) {
                log_warn("Ignoring unknown field '{}' in geometry "
                         "transformation description.",
                         field);
                show_warning = false;
            }
        }

        lua_pop(lua_state, 1);
    }
}
