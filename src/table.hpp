#ifndef OSM2PGSQL_TABLE_HPP
#define OSM2PGSQL_TABLE_HPP

#include "db-copy-mgr.hpp"
#include "osmtypes.hpp"
#include "pgsql.hpp"
#include "taginfo.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <boost/optional.hpp>

typedef std::vector<std::string> hstores_t;

class table_t
{
public:
    table_t(std::string const &name, std::string const &type,
            columns_t const &columns, hstores_t const &hstore_columns,
            const int srid, const bool append, const int hstore_mode,
            std::shared_ptr<db_copy_thread_t> const &copy_thread);
    table_t(const table_t &other,
            std::shared_ptr<db_copy_thread_t> const &copy_thread);

    void start(std::string const &conninfo,
               boost::optional<std::string> const &table_space);
    void stop(bool updateable, bool enable_hstore_index,
              boost::optional<std::string> const &table_space_index);

    void commit();

    void write_row(osmid_t id, taglist_t const &tags, std::string const &geom);
    void delete_row(const osmid_t id);

    // interface for retrieving well known binary geometry from the table
    class wkb_reader
    {
        friend table_t;

    public:
        const char *get_next()
        {
            if (m_current < m_count) {
                return m_result.get_value(m_current++, 0);
            }
            return nullptr;
        }

        int get_count() const { return m_count; }

    private:
        wkb_reader(pg_result_t &&result)
        : m_result(std::move(result)), m_count(m_result.num_tuples()),
          m_current(0)
        {}

        pg_result_t m_result;
        int m_count;
        int m_current;
    };
    wkb_reader get_wkb_reader(const osmid_t id);

protected:
    void connect();
    void prepare();
    void teardown();

    void write_columns(taglist_t const &tags, std::vector<bool> *used);
    void write_tags_column(taglist_t const &tags,
                           std::vector<bool> const &used);
    void write_hstore_columns(taglist_t const &tags);

    void escape_type(std::string const &value, ColumnType flags);

    void generate_copy_column_list();

    std::string m_conninfo;
    std::shared_ptr<db_target_descr_t> m_target;
    std::string type;
    std::unique_ptr<pg_conn_t> m_sql_conn;
    std::string srid;
    bool append;
    int hstore_mode;
    columns_t columns;
    hstores_t hstore_columns;
    std::string m_table_space;

    db_copy_mgr_t<db_deleter_by_id_t> m_copy;
};

#endif // OSM2PGSQL_TABLE_HPP
