#ifndef OSM2PGSQL_FLEX_TABLE_HPP
#define OSM2PGSQL_FLEX_TABLE_HPP

#include "db-copy-mgr.hpp"
#include "flex-table-column.hpp"
#include "pgsql.hpp"

#include <osmium/osm/item_type.hpp>

#include <cstddef>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

/**
 * An output table (in the SQL sense) for the flex backend.
 */
class flex_table_t
{
public:
    flex_table_t(std::string const &name, int srid,
                 std::shared_ptr<db_copy_thread_t> const &copy_thread,
                 bool append)
    : m_name(name), m_srid(srid), m_copy_mgr(copy_thread), m_append(append)
    {}

    std::string const &name() const noexcept { return m_name; }

    std::string const &schema() const noexcept { return m_schema; }

    void set_schema(std::string const &schema) noexcept { m_schema = schema; }

    osmium::item_type id_type() const noexcept { return m_id_type; }

    void set_id_type(osmium::item_type type) noexcept
    {
        m_id_type = type;
        m_has_id_column = true;
    }

    std::size_t num_columns() const noexcept { return m_columns.size(); }

    std::vector<flex_table_column_t>::const_iterator begin() const noexcept
    {
        return m_columns.begin();
    }

    std::vector<flex_table_column_t>::const_iterator end() const noexcept
    {
        return m_columns.end();
    }

    char const *id_column_name() const noexcept;

    bool has_geom_column() const noexcept
    {
        return m_geom_column != std::numeric_limits<std::size_t>::max();
    }

    // XXX should we allow several geometry columns?
    flex_table_column_t const &geom_column() const noexcept
    {
        assert(has_geom_column());
        return m_columns[m_geom_column];
    }

    int srid() const noexcept { return m_srid; }

    std::string build_sql_prepare_get_wkb() const;

    std::string build_sql_create_table(bool final_table) const;

    std::string build_sql_column_list() const;

    /// Does this table take objects of the specified type?
    bool matches_type(osmium::item_type type) const noexcept
    {
        if (type == m_id_type) {
            return true;
        }
        return m_id_type == osmium::item_type::area &&
               type != osmium::item_type::node;
    }

    /// Map way/node/relation ID to id value used in database table column
    osmid_t map_id(osmium::item_type type, osmid_t id) const noexcept
    {
        if (m_id_type == osmium::item_type::area &&
            type == osmium::item_type::relation) {
            return -id;
        }
        return id;
    }

    flex_table_column_t &add_column(std::string const &name,
                                    std::string const &type);

    void init()
    {
        std::string const columns = build_sql_column_list();
        m_target = std::make_shared<db_target_descr_t>(
            name().c_str(), id_column_name(), columns.c_str());
    }

    void connect(std::string const &conninfo);

    void commit() { m_copy_mgr.sync(); }

    void new_line() { m_copy_mgr.new_line(m_target); }

    void teardown() { m_db_connection.reset(); }

    void prepare()
    {
        assert(m_db_connection);
        if (m_has_id_column) {
            m_db_connection->exec(build_sql_prepare_get_wkb());
        }
    }

    void start(std::string const &conninfo, std::string const &table_space);

    void stop(bool updateable, std::string const &table_space_index);

    void delete_rows_with(osmid_t id);

    pg_result_t get_geom_by_id(osmid_t id) const;

    db_copy_mgr_t<db_deleter_by_id_t> *copy_mgr() noexcept
    {
        return &m_copy_mgr;
    }

private:
    std::string full_name() const;
    std::string full_tmp_name() const;

    /// The name of the table
    std::string m_name;

    /// The schema this table is in
    std::string m_schema{"public"};

    /**
     * Either empty for the default PostgreSQL tablespace, or " TABLESPACE "
     * plus a tablespace name.
     */
    std::string m_table_space;

    /**
     * The columns in this table (The first zero, one or two columns are always
     * the id columns).
     */
    std::vector<flex_table_column_t> m_columns;

    /// Index of the geometry column in m_columns. Default means no geometry.
    std::size_t m_geom_column = std::numeric_limits<std::size_t>::max();

    /**
     * Type of Id stored in this table (node, way, relation, area, or
     * undefined for any type).
     */
    osmium::item_type m_id_type = osmium::item_type::undefined;

    /// The SRID all geometries in this table use.
    int m_srid;

    /**
     * The copy manager responsible for sending data through the COPY mechanism
     * to the database server.
     */
    db_copy_mgr_t<db_deleter_by_id_t> m_copy_mgr;

    /// The connection to the database server.
    std::unique_ptr<pg_conn_t> m_db_connection;

    std::shared_ptr<db_target_descr_t> m_target;

    /// Are we in append mode?
    bool m_append;

    /**
     * Does this table have an idea column? Tables without id columns are
     * possible, but can not be updated.
     */
    bool m_has_id_column = false;

}; // class flex_table_t

#endif // OSM2PGSQL_FLEX_TABLE_HPP
