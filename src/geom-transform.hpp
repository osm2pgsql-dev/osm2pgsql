#ifndef OSM2PGSQL_GEOM_TRANSFORM_HPP
#define OSM2PGSQL_GEOM_TRANSFORM_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "flex-table-column.hpp"
#include "geom.hpp"
#include "reprojection.hpp"

#include <osmium/fwd.hpp>
#include <osmium/memory/buffer.hpp>

extern "C"
{
#include <lua.h>
}

#include <memory>

/**
 * Abstract base class for geometry transformations from nodes, ways, or
 * relations to simple feature type geometries.
 */
class geom_transform_t
{
public:
    virtual ~geom_transform_t() = default;

    virtual bool set_param(char const * /*name*/, lua_State * /*lua_state*/)
    {
        return false;
    }

    virtual bool
    is_compatible_with(table_column_type geom_type) const noexcept = 0;

    virtual geom::geometry_t convert(reprojection const & /*proj*/,
                                     osmium::Node const & /*node*/) const
    {
        return {};
    }

    virtual geom::geometry_t convert(reprojection const & /*proj*/,
                                     osmium::Way const & /*way*/) const
    {
        return {};
    }

    virtual geom::geometry_t
    convert(reprojection const & /*proj*/,
            osmium::Relation const & /*relation*/,
            osmium::memory::Buffer const & /*buffer*/) const
    {
        return {};
    }

    virtual bool split() const noexcept { return false; }

}; // class geom_transform_t

class geom_transform_point_t : public geom_transform_t
{
public:
    bool
    is_compatible_with(table_column_type geom_type) const noexcept override;

    geom::geometry_t convert(reprojection const &proj,
                             osmium::Node const &node) const override;

}; // class geom_transform_point_t

class geom_transform_line_t : public geom_transform_t
{
public:
    bool set_param(char const *name, lua_State *lua_state) override;

    bool
    is_compatible_with(table_column_type geom_type) const noexcept override;

    geom::geometry_t convert(reprojection const &proj,
                             osmium::Way const &way) const override;

    geom::geometry_t
    convert(reprojection const &proj, osmium::Relation const &relation,
            osmium::memory::Buffer const &buffer) const override;

private:
    double m_split_at = 0.0;

}; // class geom_transform_line_t

class geom_transform_area_t : public geom_transform_t
{
public:
    bool set_param(char const *name, lua_State *lua_state) override;

    bool
    is_compatible_with(table_column_type geom_type) const noexcept override;

    geom::geometry_t convert(reprojection const &proj,
                             osmium::Way const &way) const override;

    geom::geometry_t
    convert(reprojection const &proj, osmium::Relation const &relation,
            osmium::memory::Buffer const &buffer) const override;

    bool split() const noexcept override { return !m_multi; }

private:
    bool m_multi = true;

}; // class geom_transform_area_t

std::unique_ptr<geom_transform_t> create_geom_transform(char const *type);

void init_geom_transform(geom_transform_t *transform, lua_State *lua_state);

#endif // OSM2PGSQL_GEOM_TRANSFORM_HPP
