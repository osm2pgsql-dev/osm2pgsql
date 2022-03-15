#ifndef OSM2PGSQL_TABLE_HPP
#define OSM2PGSQL_TABLE_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "db-copy-mgr.hpp"
#include "osmtypes.hpp"
#include "pgsql.hpp"
#include "taginfo.hpp"
#include "thread-pool.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using hstores_t = std::vector<std::string>;

class table_t
{
public:
    table_t(std::string const &name, std::string type, columns_t columns,
            hstores_t hstore_columns, int srid, bool append,
            hstore_column hstore_mode,
            std::shared_ptr<db_copy_thread_t> const &copy_thread,
            std::string const &schema);

    table_t(table_t const &other,
            std::shared_ptr<db_copy_thread_t> const &copy_thread);

    void start(std::string const &conninfo, std::string const &table_space);
    void stop(bool updateable, bool enable_hstore_index,
              std::string const &table_space_index);

    void sync();

    void task_set(std::future<std::chrono::milliseconds> &&future)
    {
        m_task_result.set(std::move(future));
    }

    void task_wait();

    void write_row(osmid_t id, taglist_t const &tags, std::string const &geom);
    void delete_row(osmid_t id);

    pg_result_t get_wkb(osmid_t id);

    task_result_t m_task_result;

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
    std::string m_type;
    std::unique_ptr<pg_conn_t> m_sql_conn;
    std::string m_srid;
    bool m_append;
    hstore_column m_hstore_mode;
    columns_t m_columns;
    hstores_t m_hstore_columns;
    std::string m_table_space;

    db_copy_mgr_t<db_deleter_by_id_t> m_copy;
};

#endif // OSM2PGSQL_TABLE_HPP
