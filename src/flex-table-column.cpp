/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2026 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "flex-table-column.hpp"

#include "format.hpp"
#include "geom-boost-adaptor.hpp"
#include "overloaded.hpp"
#include "pgsql-capabilities.hpp"
#include "projection.hpp"
#include "util.hpp"

#include <cassert>
#include <cctype>
#include <cstdlib>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

struct column_type_lookup
{
    char const *m_name;
    table_column_type m_type;

    char const *name() const noexcept { return m_name; }
};

std::vector<column_type_lookup> const COLUMN_TYPES = {
    {{"text", table_column_type::text},
     {"boolean", table_column_type::boolean},
     {"bool", table_column_type::boolean},
     {"int2", table_column_type::int2},
     {"smallint", table_column_type::int2},
     {"int4", table_column_type::int4},
     {"int", table_column_type::int4},
     {"integer", table_column_type::int4},
     {"int8", table_column_type::int8},
     {"bigint", table_column_type::int8},
     {"real", table_column_type::real},
     {"double", table_column_type::double_precision},
     {"timestamp", table_column_type::timestamp},
     {"timestamptz", table_column_type::timestamptz},
     {"hstore", table_column_type::hstore},
     {"json", table_column_type::json},
     {"jsonb", table_column_type::jsonb},
     {"direction", table_column_type::direction},
     {"geometry", table_column_type::geometry},
     {"point", table_column_type::point},
     {"linestring", table_column_type::linestring},
     {"polygon", table_column_type::polygon},
     {"multipoint", table_column_type::multipoint},
     {"multilinestring", table_column_type::multilinestring},
     {"multipolygon", table_column_type::multipolygon},
     {"geometrycollection", table_column_type::geometrycollection},
     {"id_type", table_column_type::id_type},
     {"id_num", table_column_type::id_num}}};

table_column_type get_column_type_from_string(std::string const &type)
{
    auto const *column_type = util::find_by_name(COLUMN_TYPES, type);
    if (!column_type) {
        throw fmt_error("Unknown column type '{}'.", type);
    }

    return column_type->m_type;
}

std::string lowercase(std::string const &str)
{
    std::string result;

    for (char const c : str) {
        result +=
            static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    return result;
}

} // anonymous namespace

flex_table_column_t::flex_table_column_t(std::string name,
                                         std::string const &type,
                                         std::string sql_type)
: m_name(std::move(name)), m_type_name(lowercase(type)),
  m_sql_type(std::move(sql_type)),
  m_type(get_column_type_from_string(m_type_name))
{
    if (m_type == table_column_type::hstore) {
        if (!has_extension("hstore")) {
            throw std::runtime_error{"Extension 'hstore' not available. Use "
                                     "'CREATE EXTENSION hstore;' to load it."};
        }
    }
}

void flex_table_column_t::set_projection(char const *projection)
{
    if (!projection || *projection == '\0') {
        return;
    }

    auto const proj = lowercase(projection);

    if (proj == "merc" || proj == "mercator") {
        m_srid = PROJ_SPHERE_MERC;
        return;
    }

    if (proj == "latlong" || proj == "latlon" || proj == "wgs84") {
        m_srid = PROJ_LATLONG;
        return;
    }

    char *end = nullptr;
    m_srid = static_cast<int>(std::strtoul(projection, &end, 10));

    if (*end != '\0') {
        throw fmt_error("Unknown projection: '{}'.", projection);
    }
}

std::string flex_table_column_t::sql_type_name() const
{
    if (!m_sql_type.empty()) {
        return m_sql_type;
    }

    switch (m_type) {
    case table_column_type::text:
        return "text";
    case table_column_type::boolean:
        return "boolean";
    case table_column_type::int2:
        return "int2";
    case table_column_type::int4:
        return "int4";
    case table_column_type::int8:
        return "int8";
    case table_column_type::real:
        return "real";
    case table_column_type::double_precision:
        return "double precision";
    case table_column_type::timestamp:
        return "timestamp";
    case table_column_type::timestamptz:
        return "timestamptz";
    case table_column_type::hstore:
        return "hstore";
    case table_column_type::json:
        return "json";
    case table_column_type::jsonb:
        return "jsonb";
    case table_column_type::direction:
        return "int2";
    case table_column_type::geometry:
        return fmt::format("Geometry(GEOMETRY, {})", m_srid);
    case table_column_type::point:
        return fmt::format("Geometry(POINT, {})", m_srid);
    case table_column_type::linestring:
        return fmt::format("Geometry(LINESTRING, {})", m_srid);
    case table_column_type::polygon:
        return fmt::format("Geometry(POLYGON, {})", m_srid);
    case table_column_type::multipoint:
        return fmt::format("Geometry(MULTIPOINT, {})", m_srid);
    case table_column_type::multilinestring:
        return fmt::format("Geometry(MULTILINESTRING, {})", m_srid);
    case table_column_type::multipolygon:
        return fmt::format("Geometry(MULTIPOLYGON, {})", m_srid);
    case table_column_type::geometrycollection:
        return fmt::format("Geometry(GEOMETRYCOLLECTION, {})", m_srid);
    case table_column_type::id_type:
        return "char(1)";
    case table_column_type::id_num:
        return "int8";
    }
    throw std::runtime_error{"Unknown column type."};
}

std::string flex_table_column_t::sql_modifiers() const
{
    std::string modifiers;

    if (m_not_null) {
        modifiers += "NOT NULL ";
    }

    if (!modifiers.empty()) {
        modifiers.resize(modifiers.size() - 1);
    }

    return modifiers;
}

std::string flex_table_column_t::sql_create() const
{
    return fmt::format(R"("{}" {} {})", m_name, sql_type_name(),
                       sql_modifiers());
}

void flex_table_column_t::add_expire(expire_config_t const &config)
{
    assert(is_geometry_column());
    assert(srid() == PROJ_SPHERE_MERC);
    m_expires.push_back(config);
}

namespace {

/**
 * This expires all geometries in "geoms" by themselves. Used when we don't
 * need diff expire.
 */
void separate_expire(std::vector<geom::geometry_t> const &geoms,
                     expire_config_t const &expire_config,
                     expire_tiles_t &expire_tiles,
                     std::vector<expire_output_t> *expire_outputs)
{
    assert(expire_outputs);

    for (auto const &geom : geoms) {
        expire_tiles.from_geometry(geom, expire_config);
    }
    expire_tiles.commit_tiles(&expire_outputs->at(expire_config.expire_output));
}

/**
 * When doing diff expire, we need to calculate the symmetric difference
 * between old and new geometries. The difference is done by type, so points
 * are compared with points, linestrings with linestrings, etc. This function
 * separates out the input geometries by the three fundamental types.
 */
// NOLINTBEGIN(cppcoreguidelines-rvalue-reference-param-not-moved)
template <typename T>
void classify_geometries(T input_geoms, geom::multipoint_t *points,
                         geom::multilinestring_t *linestrings,
                         geom::multipolygon_t *polygons)
{
    assert(points);
    assert(linestrings);
    assert(polygons);

    for (auto &&geom : *input_geoms) {
        visit(overloaded{
                  [&](geom::nullgeom_t && /*input*/) {},
                  [&](geom::point_t &&input) { points->add_geometry(input); },
                  [&](geom::linestring_t &&input) {
                      linestrings->add_geometry(std::move(input));
                  },
                  [&](geom::polygon_t &&input) {
                      polygons->add_geometry(std::move(input));
                  },
                  [&](geom::multipoint_t &&input) {
                      for (auto &&point : input) {
                          points->add_geometry(point);
                      }
                  },
                  [&](geom::multilinestring_t &&input) {
                      for (auto &&linestring : input) {
                          linestrings->add_geometry(std::move(linestring));
                      }
                  },
                  [&](geom::multipolygon_t &&input) {
                      for (auto &&polygon : input) {
                          polygons->add_geometry(std::move(polygon));
                      }
                  },
                  [&](geom::collection_t &&input) {
                      classify_geometries(&input, points, linestrings,
                                          polygons);
                  }},
              std::move(geom));
    }
}
// NOLINTEND(cppcoreguidelines-rvalue-reference-param-not-moved)

template <typename T>
void diff_and_expire(geom::multigeometry_t<T> const &old_geoms,
                     geom::multigeometry_t<T> const &new_geoms,
                     expire_config_t const &expire_config,
                     expire_tiles_t &expire_tiles)
{
    std::vector<T> diffs;
    boost::geometry::sym_difference(old_geoms, new_geoms, diffs);
    for (auto const &geom : diffs) {
        expire_tiles.from_geometry(geom, expire_config);
    }
}

void diff_expire(std::vector<geom::geometry_t> *geoms_old,
                 std::vector<geom::geometry_t> *geoms_new,
                 expire_config_t const &expire_config,
                 expire_tiles_t &expire_tiles,
                 std::vector<expire_output_t> *expire_outputs)
{
    assert(geoms_old);
    assert(geoms_new);
    assert(expire_outputs);

    geom::multipoint_t old_points;
    geom::multilinestring_t old_linestrings;
    geom::multipolygon_t old_polygons;

    classify_geometries(geoms_old, &old_points, &old_linestrings,
                        &old_polygons);

    geom::multipoint_t new_points;
    geom::multilinestring_t new_linestrings;
    geom::multipolygon_t new_polygons;

    classify_geometries(geoms_new, &new_points, &new_linestrings,
                        &new_polygons);

    diff_and_expire(old_points, new_points, expire_config, expire_tiles);
    diff_and_expire(old_linestrings, new_linestrings, expire_config,
                    expire_tiles);
    diff_and_expire(old_polygons, new_polygons, expire_config, expire_tiles);

    expire_tiles.commit_tiles(&expire_outputs->at(expire_config.expire_output));
}

} // anonymous namespace

void flex_table_column_t::do_expire(
    std::vector<geom::geometry_t> *geoms_old,
    std::vector<geom::geometry_t> *geoms_new,
    std::vector<expire_tiles_t> *expire,
    std::vector<expire_output_t> *expire_outputs, bool enable_diff_expire) const
{
    assert(geoms_old);
    assert(geoms_new);
    assert(expire);
    assert(expire_outputs);

    for (auto const &expire_config : m_expires) {
        assert(expire_config.expire_output < expire->size());
        auto &expire_output = expire_outputs->at(expire_config.expire_output);

        if (expire_output.has_tile_output()) {
            auto &expire_tiles = expire->at(expire_config.expire_output);
            if (!expire_config.diff_expire || !enable_diff_expire ||
                geoms_old->empty() || geoms_new->empty()) {
                separate_expire(*geoms_old, expire_config, expire_tiles,
                                expire_outputs);
                separate_expire(*geoms_new, expire_config, expire_tiles,
                                expire_outputs);
            } else {
                diff_expire(geoms_old, geoms_new, expire_config, expire_tiles,
                            expire_outputs);
            }
        }

        // Record the endpoints of the changed geometry (old and new), so a
        // consumer (e.g. the grouped-linemerge generalizer) can re-merge only
        // the exact connected components touched, instead of everything in a
        // tile. Deletes provide the old geometry via the geometry cache, so
        // their endpoints are captured here too.
        if (expire_output.has_endpoint_output()) {
            for (auto const &geom : *geoms_old) {
                expire_output.add_endpoints(geom);
            }
            for (auto const &geom : *geoms_new) {
                expire_output.add_endpoints(geom);
            }
        }
    }
}
