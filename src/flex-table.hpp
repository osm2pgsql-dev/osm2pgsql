#ifndef OSM2PGSQL_FLEX_TABLE_HPP
#define OSM2PGSQL_FLEX_TABLE_HPP

#include "db-copy-mgr.hpp"
#include "flex-table-column.hpp"
#include "osmium-builder.hpp"
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

    /**
     * Table creation type: interim tables are created as UNLOGGED and with
     * autovacuum disabled.
     */
    enum class table_type {
        interim,
        permanent
    };

    flex_table_t(std::string name) : m_name(std::move(name)) {}

    std::string const &name() const noexcept { return m_name; }

    std::string const &schema() const noexcept { return m_schema; }

    std::string const &data_tablespace() const noexcept
    {
        return m_data_tablespace;
    }

    std::string const &index_tablespace() const noexcept
    {
        return m_index_tablespace;
    }

    void set_schema(std::string const &schema) noexcept { m_schema = schema; }

    void set_data_tablespace(std::string const &tablespace) noexcept
    {
        m_data_tablespace = tablespace;
    }

    void set_index_tablespace(std::string const &tablespace) noexcept
    {
        m_index_tablespace = tablespace;
    }

    osmium::item_type id_type() const noexcept { return m_id_type; }

    void set_id_type(osmium::item_type type) noexcept { m_id_type = type; }

    bool has_id_column() const noexcept
    {
        if (m_columns.empty()) {
            return false;
        }
        return (m_columns[0].type() == table_column_type::id_type) ||
               (m_columns[0].type() == table_column_type::id_num);
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

    int srid() const noexcept
    {
        return has_geom_column() ? geom_column().srid() : 4326;
    }

    std::string build_sql_prepare_get_wkb() const;

    std::string build_sql_create_table(table_type ttype,
                                       std::string const &table_name) const;

    std::string build_sql_column_list() const;

    std::string build_sql_create_id_index() const;

    /// Does this table take objects of the specified type?
    bool matches_type(osmium::item_type type) const noexcept
    {
        // This table takes any type -> okay
        if (m_id_type == osmium::item_type::undefined) {
            return true;
        }

        // Type and table type match -> okay
        if (type == m_id_type) {
            return true;
        }

        // Relations can be written as linestrings into way tables -> okay
        if (type == osmium::item_type::relation &&
            m_id_type == osmium::item_type::way) {
            return true;
        }

        // Area tables can take ways or relations, but not nodes
        return m_id_type == osmium::item_type::area &&
               type != osmium::item_type::node;
    }

    /// Map way/node/relation ID to id value used in database table column
    osmid_t map_id(osmium::item_type type, osmid_t id) const noexcept
    {
        if (m_id_type == osmium::item_type::undefined) {
            if (has_multicolumn_id_index()) {
                return id;
            }

            switch (type) {
            case osmium::item_type::node:
                return id;
            case osmium::item_type::way:
                return -id;
            case osmium::item_type::relation:
                return -id - 100000000000000000LL;
            default:
                assert(false);
            }
        }

        if (m_id_type != osmium::item_type::relation &&
            type == osmium::item_type::relation) {
            return -id;
        }
        return id;
    }

    flex_table_column_t &add_column(std::string const &name,
                                    std::string const &type);

    bool has_multicolumn_id_index() const noexcept;
    std::string id_column_names() const;
    std::string full_name() const;
    std::string full_tmp_name() const;

private:
    /// The name of the table
    std::string m_name;

    /// The schema this table is in
    std::string m_schema{"public"};

    /// The table space used for this table (empty for default tablespace)
    std::string m_data_tablespace;

    /**
     * The table space used for indexes on this table (empty for default
     * tablespace)
     */
    std::string m_index_tablespace;

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

}; // class flex_table_t

class table_connection_t
{
public:
    table_connection_t(flex_table_t *table,
                       std::shared_ptr<db_copy_thread_t> const &copy_thread)
    : m_builder(reprojection::create_projection(table->srid())), m_table(table),
      m_target(std::make_shared<db_target_descr_t>(
          table->name(), table->id_column_names(),
          table->build_sql_column_list())),
      m_copy_mgr(copy_thread), m_db_connection(nullptr)
    {
        m_target->schema = table->schema();
    }

    void connect(std::string const &conninfo);

    void start(bool append);

    void stop(bool updateable, bool append);

    flex_table_t const &table() const noexcept { return *m_table; }

    void teardown() { m_db_connection.reset(); }

    void prepare();

    void create_id_index();

    pg_result_t get_geom_by_id(osmium::item_type type, osmid_t id) const;

    void sync() { m_copy_mgr.sync(); }

    void new_line() { m_copy_mgr.new_line(m_target); }

    db_copy_mgr_t<db_deleter_by_type_and_id_t> *copy_mgr() noexcept
    {
        return &m_copy_mgr;
    }

    void delete_rows_with(osmium::item_type type, osmid_t id);

    geom::osmium_builder_t *get_builder() { return &m_builder; }

private:
    geom::osmium_builder_t m_builder;

    flex_table_t *m_table;

    std::shared_ptr<db_target_descr_t> m_target;

    /**
     * The copy manager responsible for sending data through the COPY mechanism
     * to the database server.
     */
    db_copy_mgr_t<db_deleter_by_type_and_id_t> m_copy_mgr;

    /// The connection to the database server.
    std::unique_ptr<pg_conn_t> m_db_connection;

    /// Has the Id index already been created?
    bool m_id_index_created = false;

}; // class table_connection_t

char const *type_to_char(osmium::item_type type) noexcept;

#endif // OSM2PGSQL_FLEX_TABLE_HPP
