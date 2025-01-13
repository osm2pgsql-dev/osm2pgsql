/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2025 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "flex-table.hpp"
#include "format.hpp"
#include "logging.hpp"
#include "pgsql-capabilities.hpp"
#include "pgsql-helper.hpp"
#include "util.hpp"

#include <algorithm>
#include <cassert>
#include <string>

char const *type_to_char(osmium::item_type type) noexcept
{
    switch (type) {
    case osmium::item_type::node:
        return "N";
    case osmium::item_type::way:
        return "W";
    case osmium::item_type::relation:
        return "R";
    default:
        break;
    }
    return "X";
}

bool flex_table_t::has_multicolumn_id_index() const noexcept
{
    return m_columns[0].type() == table_column_type::id_type;
}

std::string flex_table_t::id_column_names() const
{
    std::string name;

    if (!has_id_column()) {
        return name;
    }

    name = m_columns[0].name();
    if (has_multicolumn_id_index()) {
        name += ',';
        name += m_columns[1].name();
    }

    return name;
}

std::string flex_table_t::full_name() const
{
    return qualified_name(schema(), name());
}

std::string flex_table_t::full_tmp_name() const
{
    return qualified_name(schema(), name() + "_tmp");
}

bool flex_table_t::has_id_column() const noexcept
{
    if (m_columns.empty()) {
        return false;
    }
    return (m_columns[0].type() == table_column_type::id_type) ||
           (m_columns[0].type() == table_column_type::id_num);
}

bool flex_table_t::matches_type(osmium::item_type type) const noexcept
{
    // This table takes any type -> okay
    if (m_id_type == flex_table_index_type::any_object) {
        return true;
    }

    // Type and table type match -> okay
    if ((type == osmium::item_type::node &&
         m_id_type == flex_table_index_type::node) ||
        (type == osmium::item_type::way &&
         m_id_type == flex_table_index_type::way) ||
        (type == osmium::item_type::relation &&
         m_id_type == flex_table_index_type::relation)) {
        return true;
    }

    // Relations can be written as linestrings into way tables -> okay
    if (type == osmium::item_type::relation &&
        m_id_type == flex_table_index_type::way) {
        return true;
    }

    // Area tables can take ways or relations, but not nodes
    return m_id_type == flex_table_index_type::area &&
           type != osmium::item_type::node;
}

/// Map way/node/relation ID to id value used in database table column
osmid_t flex_table_t::map_id(osmium::item_type type, osmid_t id) const noexcept
{
    if (m_id_type == flex_table_index_type::any_object) {
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

    if (m_id_type != flex_table_index_type::relation &&
        type == osmium::item_type::relation) {
        return -id;
    }
    return id;
}

flex_table_column_t &flex_table_t::add_column(std::string const &name,
                                              std::string const &type,
                                              std::string const &sql_type)
{
    // id_type (optional) and id_num must always be the first columns
    assert(type != "id_type" || m_columns.empty());
    assert(type != "id_num" || m_columns.empty() ||
           (m_columns.size() == 1 &&
            m_columns[0].type() == table_column_type::id_type));

    auto &column = m_columns.emplace_back(name, type, sql_type);

    if (column.is_geometry_column()) {
        if (m_geom_column == std::numeric_limits<std::size_t>::max()) {
            m_geom_column = m_columns.size() - 1;
        } else {
            m_has_multiple_geom_columns = true;
        }
    }

    return column;
}

std::string flex_table_t::build_sql_prepare_get_wkb() const
{
    util::string_joiner_t joiner{',', '"'};
    for (auto const &column : m_columns) {
        if (!column.expire_configs().empty()) {
            joiner.add(column.name());
        }
    }

    assert(!joiner.empty());

    std::string const columns = joiner();

    if (has_multicolumn_id_index()) {
        return fmt::format(
            R"(SELECT {} FROM {} WHERE "{}" = $1::char(1) AND "{}" = $2::bigint)",
            columns, full_name(), m_columns[0].name(), m_columns[1].name());
    }

    return fmt::format(R"(SELECT {} FROM {} WHERE "{}" = $1::bigint)", columns,
                       full_name(), id_column_names());
}

std::string
flex_table_t::build_sql_create_table(table_type ttype,
                                     std::string const &table_name) const
{
    assert(!m_columns.empty());

    std::string sql =
        fmt::format("CREATE {} TABLE IF NOT EXISTS {} (",
                    ttype == table_type::interim ? "UNLOGGED" : "", table_name);

    util::string_joiner_t joiner{','};
    for (auto const &column : m_columns) {
        // create_only columns are only created in permanent, not in the
        // interim tables
        if (ttype == table_type::permanent || !column.create_only()) {
            joiner.add(column.sql_create());
        }
    }

    sql += joiner();
    sql += ')';

    if (ttype == table_type::interim) {
        sql += " WITH (autovacuum_enabled = off)";
    }

    sql += tablespace_clause(m_data_tablespace);

    return sql;
}

std::string flex_table_t::build_sql_column_list() const
{
    assert(!m_columns.empty());

    util::string_joiner_t joiner{',', '"'};

    for (auto const &column : m_columns) {
        if (!column.create_only()) {
            joiner.add(column.name());
        }
    }

    return joiner();
}

std::string flex_table_t::build_sql_create_id_index() const
{
    return fmt::format("CREATE {}INDEX ON {} USING BTREE ({}) {}",
                       m_build_unique_id_index ? "UNIQUE " : "", full_name(),
                       id_column_names(),
                       tablespace_clause(index_tablespace()));
}

flex_index_t &flex_table_t::add_index(std::string method)
{
    return m_indexes.emplace_back(std::move(method));
}

bool flex_table_t::has_columns_with_expire() const noexcept
{
    return std::any_of(m_columns.cbegin(), m_columns.cend(),
                       [](auto const &column) { return column.has_expire(); });
}

void flex_table_t::prepare(pg_conn_t const &db_connection) const
{
    if (has_id_column() && has_columns_with_expire()) {
        auto const stmt = fmt::format("get_wkb_{}", m_table_num);
        db_connection.prepare(stmt, fmt::runtime(build_sql_prepare_get_wkb()));
    }
}

void flex_table_t::analyze(pg_conn_t const &db_connection) const
{
    analyze_table(db_connection, schema(), name());
}

namespace {

void enable_check_trigger(pg_conn_t const &db_connection,
                          flex_table_t const &table)
{
    std::string checks;

    for (auto const &column : table.columns()) {
        if (column.is_geometry_column() && column.needs_isvalid()) {
            checks.append(fmt::format(
                R"((NEW."{0}" IS NULL OR ST_IsValid(NEW."{0}")) AND )",
                column.name()));
        }
    }

    if (checks.empty()) {
        return;
    }

    // remove last " AND "
    checks.resize(checks.size() - 5);

    create_geom_check_trigger(db_connection, table.schema(), table.name(),
                              checks);
}

} // anonymous namespace

void table_connection_t::start(pg_conn_t const &db_connection,
                               bool append) const
{
    if (!append) {
        drop_table_if_exists(db_connection, table().schema(), table().name());
    }

    // These _tmp tables can be left behind if we run out of disk space.
    drop_table_if_exists(db_connection, table().schema(),
                         table().name() + "_tmp");

    if (!append) {
        db_connection.exec(table().build_sql_create_table(
            table().cluster_by_geom() ? flex_table_t::table_type::interim
                                      : flex_table_t::table_type::permanent,
            table().full_name()));

        enable_check_trigger(db_connection, table());
    }

    table().prepare(db_connection);
}

void table_connection_t::stop(pg_conn_t const &db_connection, bool updateable,
                              bool append)
{
    m_copy_mgr.sync();

    if (append) {
        return;
    }

    if (table().cluster_by_geom()) {
        if (table().geom_column().needs_isvalid()) {
            drop_geom_check_trigger(db_connection, table().schema(),
                                    table().name());
        }

        log_info("Clustering table '{}' by geometry...", table().name());

        db_connection.exec(table().build_sql_create_table(
            flex_table_t::table_type::permanent, table().full_tmp_name()));

        std::string const columns = table().build_sql_column_list();

        auto const geom_column_name =
            "\"" + table().geom_column().name() + "\"";

        std::string const sql =
            fmt::format("INSERT INTO {} ({}) SELECT {} FROM {} ORDER BY {}",
                        table().full_tmp_name(), columns, columns,
                        table().full_name(), geom_column_name);

        db_connection.exec(sql);

        db_connection.exec("DROP TABLE {}", table().full_name());
        db_connection.exec(R"(ALTER TABLE {} RENAME TO "{}")",
                           table().full_tmp_name(), table().name());
        m_id_index_created = false;

        if (updateable) {
            enable_check_trigger(db_connection, table());
        }
    }

    if (table().indexes().empty()) {
        log_info("No indexes to create on table '{}'.", table().name());
    } else {
        for (auto const &index : table().indexes()) {
            log_info("Creating index on table '{}' {}...", table().name(),
                     index.columns());
            auto const sql = index.create_index(
                qualified_name(table().schema(), table().name()));
            db_connection.exec(sql);
        }
    }

    if ((table().always_build_id_index() || updateable) &&
        table().has_id_column()) {
        create_id_index(db_connection);
    }

    log_info("Analyzing table '{}'...", table().name());
    table().analyze(db_connection);
}

void table_connection_t::create_id_index(pg_conn_t const &db_connection)
{
    if (m_id_index_created) {
        log_debug("Id index on table '{}' already created.", table().name());
    } else {
        log_info("Creating id index on table '{}'...", table().name());
        db_connection.exec(table().build_sql_create_id_index());
        m_id_index_created = true;
    }
}

pg_result_t table_connection_t::get_geoms_by_id(pg_conn_t const &db_connection,
                                                osmium::item_type type,
                                                osmid_t id) const
{
    assert(table().has_geom_column());
    std::string const stmt = fmt::format("get_wkb_{}", table().num());
    if (table().has_multicolumn_id_index()) {
        return db_connection.exec_prepared_as_binary(stmt.c_str(),
                                                     type_to_char(type), id);
    }
    return db_connection.exec_prepared_as_binary(stmt.c_str(), id);
}

void table_connection_t::delete_rows_with(osmium::item_type type, osmid_t id)
{
    m_copy_mgr.new_line(m_target);

    if (!table().has_multicolumn_id_index()) {
        type = osmium::item_type::undefined;
    }
    m_copy_mgr.delete_object(type_to_char(type)[0], id);
}

void table_connection_t::task_wait()
{
    auto const run_time = m_task_result.wait();
    log_info("All postprocessing on table '{}' done in {}.", table().name(),
             util::human_readable_duration(run_time));
    log_debug("Inserted {} rows into table '{}' ({} not inserted due to"
              " NOT NULL constraints).",
              m_count_insert, table().name(), m_count_not_null_error);
}
