/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "flex-table-column.hpp"
#include "format.hpp"
#include "pgsql-capabilities.hpp"
#include "util.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <stdexcept>
#include <utility>

struct column_type_lookup
{
    char const *m_name;
    table_column_type type;

    char const *name() const noexcept { return m_name; }
};

static std::array<column_type_lookup, 26> const column_types = {
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
     {"area", table_column_type::area},
     {"id_type", table_column_type::id_type},
     {"id_num", table_column_type::id_num}}};

static table_column_type get_column_type_from_string(std::string const &type)
{
    auto const *column_type = util::find_by_name(column_types, type);
    if (!column_type) {
        throw fmt_error("Unknown column type '{}'.", type);
    }

    return column_type->type;
}

static std::string lowercase(std::string const &str)
{
    std::string result;

    for (char const c : str) {
        result +=
            static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    return result;
}

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
        m_srid = 3857;
        return;
    }

    if (proj == "latlong" || proj == "latlon" || proj == "wgs84") {
        m_srid = 4326;
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
    case table_column_type::area:
        return "real";
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
