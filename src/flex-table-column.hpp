#ifndef OSM2PGSQL_FLEX_TABLE_COLUMN_HPP
#define OSM2PGSQL_FLEX_TABLE_COLUMN_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2025 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "expire-config.hpp"
#include "expire-tiles.hpp"
#include "geom.hpp"
#include "projection.hpp"

#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

enum class table_column_type : uint8_t
{
    text,

    boolean,

    int2,
    int4,
    int8,

    real,

    hstore,
    json,
    jsonb,

    direction,

    geometry,
    point,
    linestring,
    polygon,
    multipoint,
    multilinestring,
    multipolygon,
    geometrycollection,

    id_type,
    id_num
};

/**
 * A column in a flex_table_t.
 */
class flex_table_column_t
{
public:
    flex_table_column_t(std::string name, std::string const &type,
                        std::string sql_type);

    std::string const &name() const noexcept { return m_name; }

    table_column_type type() const noexcept { return m_type; }

    bool is_point_column() const noexcept
    {
        return (m_type == table_column_type::point) ||
               (m_type == table_column_type::multipoint);
    }

    bool is_linestring_column() const noexcept
    {
        return (m_type == table_column_type::linestring) ||
               (m_type == table_column_type::multilinestring);
    }

    bool is_polygon_column() const noexcept
    {
        return (m_type == table_column_type::geometry) ||
               (m_type == table_column_type::polygon) ||
               (m_type == table_column_type::multipolygon);
    }

    bool is_geometry_column() const noexcept
    {
        return (m_type >= table_column_type::geometry) &&
               (m_type <= table_column_type::geometrycollection);
    }

    /**
     * Do we need an ST_IsValid() check in the database for this geometry
     * column? If the SRID is 4326 the geometry validity is already assured
     * by libosmium, so we don't need it. And Point geometries are always
     * valid.
     * No checks are needed for create_only columns, because they don't
     * contain anything.
     */
    bool needs_isvalid() const noexcept
    {
        assert(is_geometry_column());
        return !m_create_only && m_srid != PROJ_LATLONG &&
               m_type != table_column_type::point;
    }

    std::string const &type_name() const noexcept { return m_type_name; }

    bool not_null() const noexcept { return m_not_null; }

    bool create_only() const noexcept { return m_create_only; }

    void set_not_null(bool value = true) noexcept { m_not_null = value; }

    void set_create_only(bool value = true) noexcept { m_create_only = value; }

    void set_projection(char const *projection);

    std::string sql_type_name() const;
    std::string sql_modifiers() const;
    std::string sql_create() const;

    int srid() const noexcept { return m_srid; }

    void add_expire(expire_config_t const &config);

    bool has_expire() const noexcept { return !m_expires.empty(); }

    std::vector<expire_config_t> const &expire_configs() const noexcept
    {
        return m_expires;
    }

    void do_expire(geom::geometry_t const &geom,
                   std::vector<expire_tiles> *expire) const;

private:
    std::vector<expire_config_t> m_expires;

    /// The name of the database table column.
    std::string m_name;

    /**
     * The type name of the column.
     */
    std::string m_type_name;

    /**
     * The SQL type of the database table column. If this is not set, use
     * one generated from the m_type.
     */
    std::string m_sql_type;

    /**
     * The type of column.
     */
    table_column_type m_type;

    /**
     * For geometry columns only: The projection SRID. Default is web mercator.
     */
    int m_srid = PROJ_SPHERE_MERC;

    /// NOT NULL constraint
    bool m_not_null = false;

    /// Column will be created but not filled by osm2pgsql.
    bool m_create_only = false;
};

#endif // OSM2PGSQL_FLEX_TABLE_COLUMN_HPP
