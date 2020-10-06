#ifndef OSM2PGSQL_FLEX_TABLE_COLUMN_HPP
#define OSM2PGSQL_FLEX_TABLE_COLUMN_HPP

#include "format.hpp"
#include "reprojection.hpp"

#include <cstdint>
#include <string>

enum class table_column_type : uint8_t
{
    sql,

    text,

    boolean,

    int2,
    int4,
    int8,

    real,

    hstore,

    direction,

    geometry,
    point,
    linestring,
    polygon,
    multipoint,
    multilinestring,
    multipolygon,

    area,

    id_type,
    id_num
};

/**
 * A column in a flex_table_t.
 */
class flex_table_column_t
{
public:
    flex_table_column_t(std::string name, std::string const &type);

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
               (m_type <= table_column_type::multipolygon);
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

private:
    /// The name of the database table column.
    std::string m_name;

    /**
     * The type name of the database table column. Either a name we recognize
     * or just an SQL snippet.
     */
    std::string m_type_name;

    /**
     * The type of database column. Use table_column_type::sql as fallback
     * in which case m_type_name is the SQL type used in the database.
     */
    table_column_type m_type;

    /**
     * For geometry and area columns only: The projection SRID. Default is
     * web mercator.
     */
    int m_srid = 3857;

    /// NOT NULL constraint
    bool m_not_null = false;

    /// Column will be created but not filled by osm2pgsql.
    bool m_create_only = false;
};

#endif // OSM2PGSQL_FLEX_TABLE_COLUMN_HPP
