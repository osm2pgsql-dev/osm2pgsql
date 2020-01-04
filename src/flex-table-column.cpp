#include "flex-table-column.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <stdexcept>
#include <utility>

using column_type_lookup = std::pair<char const *, table_column_type>;

static column_type_lookup const column_types[] = {
    {"sql", table_column_type::sql},
    {"text", table_column_type::text},
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
    {"direction", table_column_type::direction},
    {"geometry", table_column_type::geometry},
    {"point", table_column_type::point},
    {"linestring", table_column_type::linestring},
    {"polygon", table_column_type::polygon},
    {"multipoint", table_column_type::multipoint},
    {"multilinestring", table_column_type::multilinestring},
    {"multipolygon", table_column_type::multipolygon},
    {"area", table_column_type::area},
    {"id_type", table_column_type::id_type},
    {"id_num", table_column_type::id_num}};

static table_column_type
get_column_type_from_string(std::string const &type) noexcept
{
    auto const it =
        std::find_if(std::begin(column_types), std::end(column_types),
                     [&type](column_type_lookup name_type) {
                         return type == name_type.first;
                     });

    if (it != std::end(column_types)) {
        return it->second;
    }

    // If we don't recognize the column type, we just assume its a valid SQL
    // type and use it "as is".
    return table_column_type::sql;
}

static std::string lowercase(std::string const &str)
{
    std::string result;

    for (char c : str) {
        result +=
            static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    return result;
}

flex_table_column_t::flex_table_column_t(std::string name,
                                         std::string const &type)
: m_name(std::move(name)), m_type_name(lowercase(type)),
  m_type(get_column_type_from_string(m_type_name))
{}

std::string flex_table_column_t::sql_type_name(int srid) const
{
    switch (m_type) {
    case table_column_type::sql:
        return m_type_name;
        break;
    case table_column_type::text:
        return "TEXT";
        break;
    case table_column_type::boolean:
        return "BOOLEAN";
        break;
    case table_column_type::int2:
        return "INT2";
        break;
    case table_column_type::int4:
        return "INT4";
        break;
    case table_column_type::int8:
        return "INT8";
        break;
    case table_column_type::real:
        return "REAL";
        break;
    case table_column_type::hstore:
        return "HSTORE";
        break;
    case table_column_type::direction:
        return "INT4";
        break;
    case table_column_type::geometry:
        return "GEOMETRY(GEOMETRY, {})"_format(srid);
        break;
    case table_column_type::point:
        return "GEOMETRY(POINT, {})"_format(srid);
        break;
    case table_column_type::linestring:
        return "GEOMETRY(LINESTRING, {})"_format(srid);
        break;
    case table_column_type::polygon:
        return "GEOMETRY(POLYGON, {})"_format(srid);
        break;
    case table_column_type::multipoint:
        return "GEOMETRY(MULTIPOINT, {})"_format(srid);
        break;
    case table_column_type::multilinestring:
        return "GEOMETRY(MULTILINESTRING, {})"_format(srid);
        break;
    case table_column_type::multipolygon:
        return "GEOMETRY(MULTIPOLYGON, {})"_format(srid);
        break;
    case table_column_type::area:
        return "REAL";
        break;
    case table_column_type::id_type:
        return "CHAR(1)";
        break;
    case table_column_type::id_num:
        return "INT8";
        break;
    }
    throw std::runtime_error{"Unknown column type"};
}

std::string flex_table_column_t::sql_modifiers() const
{
    std::string modifiers;

    if ((m_flags & table_column_flags::not_null) != 0U) {
        modifiers += "NOT NULL ";
    }

    if (!modifiers.empty()) {
        modifiers.resize(modifiers.size() - 1);
    }

    return modifiers;
}

std::string flex_table_column_t::sql_create(int srid) const
{
    return "\"{}\" {} {},"_format(m_name, sql_type_name(srid), sql_modifiers());
}
