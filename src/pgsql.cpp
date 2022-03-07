/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

/* Helper functions for the postgresql connections */
#include "format.hpp"
#include "logging.hpp"
#include "pgsql.hpp"
#include "util.hpp"

#include <array>
#include <cassert>
#include <stdexcept>
#include <string>
#include <utility>

pg_conn_t::pg_conn_t(std::string const &conninfo)
: m_conn(PQconnectdb(conninfo.c_str()))
{
    if (!m_conn) {
        throw std::runtime_error{"Connecting to database failed."};
    }
    if (PQstatus(m_conn.get()) != CONNECTION_OK) {
        throw std::runtime_error{
            "Connecting to database failed: {}."_format(error_msg())};
    }
}

char const *pg_conn_t::error_msg() const noexcept
{
    assert(m_conn);

    return PQerrorMessage(m_conn.get());
}

pg_result_t pg_conn_t::query(ExecStatusType expect, char const *sql) const
{
    assert(m_conn);

    log_sql("{}", sql);
    pg_result_t res{PQexec(m_conn.get(), sql)};
    if (PQresultStatus(res.get()) != expect) {
        throw std::runtime_error{"Database error: {}"_format(error_msg())};
    }
    return res;
}

pg_result_t pg_conn_t::query(ExecStatusType expect,
                             std::string const &sql) const
{
    return query(expect, sql.c_str());
}

void pg_conn_t::set_config(char const *setting, char const *value) const
{
    // Update pg_settings instead of using SET because it does not yield
    // errors on older versions of PostgreSQL where the settings are not
    // implemented.
    auto const sql =
        "UPDATE pg_settings SET setting = '{}' WHERE name = '{}'"_format(
            value, setting);
    query(PGRES_TUPLES_OK, sql);
}

void pg_conn_t::exec(char const *sql) const
{
    if (sql && sql[0] != '\0') {
        query(PGRES_COMMAND_OK, sql);
    }
}

void pg_conn_t::exec(std::string const &sql) const
{
    if (!sql.empty()) {
        query(PGRES_COMMAND_OK, sql.c_str());
    }
}

void pg_conn_t::copy_data(std::string const &sql,
                          std::string const &context) const
{
    assert(m_conn);

    log_sql_data("Copy data to '{}':\n{}", context, sql);
    int const r = PQputCopyData(m_conn.get(), sql.c_str(), (int)sql.size());

    switch (r) {
    case 0: // need to wait for write ready
        log_error("{} - COPY unexpectedly busy", context);
        break;
    case 1: // success
        return;
    case -1: // error occurred
        log_error("{} - error on COPY: {}", context, error_msg());
        break;
    default: // unexpected result
        break;
    }

    if (sql.size() < 1100) {
        log_error("Data: {}", sql);
    } else {
        log_error("Data: {}\n...\n{}", std::string(sql, 0, 500),
                  std::string(sql, sql.size() - 500));
    }

    throw std::runtime_error{"COPYing data to Postgresql."};
}

void pg_conn_t::end_copy(std::string const &context) const
{
    assert(m_conn);

    if (PQputCopyEnd(m_conn.get(), nullptr) != 1) {
        throw std::runtime_error{"Ending COPY mode for '{}' failed: {}."_format(
            context, error_msg())};
    }

    pg_result_t const res{PQgetResult(m_conn.get())};
    if (PQresultStatus(res.get()) != PGRES_COMMAND_OK) {
        throw std::runtime_error{fmt::format(
            "Ending COPY mode for '{}' failed: {}.", context, error_msg())};
    }
}

static std::string concat_params(int num_params,
                                 char const *const *param_values)
{
    std::string params;

    for (int i = 0; i < num_params; ++i) {
        params += param_values[i] ? param_values[i] : "<NULL>";
        params += ',';
    }

    if (!params.empty()) {
        params.resize(params.size() - 1);
    }

    return params;
}

pg_result_t
pg_conn_t::exec_prepared_internal(char const *stmt, int num_params,
                                  char const *const *param_values) const
{
    assert(m_conn);

    if (get_logger().log_sql()) {
        log_sql("EXECUTE {}({})", stmt,
                concat_params(num_params, param_values));
    }
    pg_result_t res{PQexecPrepared(m_conn.get(), stmt, num_params, param_values,
                                   nullptr, nullptr, 0)};
    if (PQresultStatus(res.get()) != PGRES_TUPLES_OK) {
        log_error("SQL command failed: EXECUTE {}({})", stmt,
                  concat_params(num_params, param_values));
        throw std::runtime_error{"Database error: {} ({})"_format(
            error_msg(), PQresultStatus(res.get()))};
    }

    return res;
}

std::string tablespace_clause(std::string const &name)
{
    std::string sql;

    if (!name.empty()) {
        sql += " TABLESPACE \"";
        sql += name;
        sql += '"';
    }

    return sql;
}

std::string qualified_name(std::string const &schema, std::string const &name)
{
    std::string result{"\""};

    if (!schema.empty()) {
        result.reserve(schema.size() + name.size() + 5);
        result += schema;
        result += "\".\"";
    } else {
        result.reserve(name.size() + 2);
    }

    result += name;
    result += '"';

    return result;
}

std::map<std::string, std::string>
get_postgresql_settings(pg_conn_t const &db_connection)
{
    auto const res = db_connection.query(
        PGRES_TUPLES_OK, "SELECT name, setting FROM pg_settings");

    std::map<std::string, std::string> settings;
    for (int i = 0; i < res.num_tuples(); ++i) {
        settings[res.get_value_as_string(i, 0)] = res.get_value_as_string(i, 1);
    }

    return settings;
}

static std::string get_database_name(pg_conn_t const &db_connection)
{
    auto const res =
        db_connection.query(PGRES_TUPLES_OK, "SELECT current_catalog");

    if (res.num_tuples() != 1) {
        throw std::runtime_error{
            "Database error: Can not access database name."};
    }

    return res.get_value_as_string(0, 0);
}

postgis_version get_postgis_version(pg_conn_t const &db_connection)
{
    auto const res = db_connection.query(
        PGRES_TUPLES_OK, "SELECT regexp_split_to_table(extversion, '\\.') FROM"
                         " pg_extension WHERE extname='postgis'");

    if (res.num_tuples() == 0) {
        throw std::runtime_error{
            "The postgis extension is not enabled on the database '{}'."
            " Are you using the correct database?"
            " Enable with 'CREATE EXTENSION postgis;'"_format(
                get_database_name(db_connection))};
    }

    return {std::stoi(res.get_value_as_string(0, 0)),
            std::stoi(res.get_value_as_string(1, 0))};
}
