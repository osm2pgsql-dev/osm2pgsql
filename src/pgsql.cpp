/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

/* Helper functions for the postgresql connections */
#include "format.hpp"
#include "logging.hpp"
#include "pgsql.hpp"
#include "util.hpp"

#include <cassert>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <utility>

std::size_t pg_result_t::affected_rows() const noexcept
{
    char const *const s = PQcmdTuples(m_result.get());
    return std::strtoull(s, nullptr, 10);
}

std::atomic<std::uint32_t> pg_conn_t::connection_id{0};

pg_conn_t::pg_conn_t(std::string const &conninfo)
: m_conn(PQconnectdb(conninfo.c_str())),
  m_connection_id(connection_id.fetch_add(1))
{
    if (!m_conn) {
        throw std::runtime_error{"Connecting to database failed."};
    }
    if (PQstatus(m_conn.get()) != CONNECTION_OK) {
        throw fmt_error("Connecting to database failed: {}.", error_msg());
    }

    if (get_logger().log_sql()) {
        auto const results = exec("SELECT pg_backend_pid()");
        log_sql("(C{}) New database connection (backend_pid={})",
                m_connection_id, results.get(0, 0));
    }

    // PostgreSQL sends notices in many different contexts which aren't that
    // useful for the user. So we disable them for all connections.
    if (!get_logger().debug_enabled()) {
        exec("SET client_min_messages = WARNING");
    }
}

void pg_conn_t::close()
{
    log_sql("(C{}) Closing database connection", m_connection_id);
    m_conn.reset();
}

char const *pg_conn_t::error_msg() const noexcept
{
    assert(m_conn);

    return PQerrorMessage(m_conn.get());
}

void pg_conn_t::set_config(char const *setting, char const *value) const
{
    // Update pg_settings instead of using SET because it does not yield
    // errors on older versions of PostgreSQL where the settings are not
    // implemented.
    exec("UPDATE pg_settings SET setting = '{}' WHERE name = '{}'", value,
         setting);
}

pg_result_t pg_conn_t::exec(char const *sql) const
{
    assert(m_conn);

    log_sql("(C{}) {}", m_connection_id, sql);
    pg_result_t res{PQexec(m_conn.get(), sql)};
    if (res.status() != PGRES_COMMAND_OK && res.status() != PGRES_TUPLES_OK) {
        throw fmt_error("Database error: {}", error_msg());
    }
    return res;
}

pg_result_t pg_conn_t::exec(std::string const &sql) const
{
    return exec(sql.c_str());
}

void pg_conn_t::copy_start(char const *sql) const
{
    assert(m_conn);

    log_sql("(C{}) {}", m_connection_id, sql);
    pg_result_t const res{PQexec(m_conn.get(), sql)};
    if (res.status() != PGRES_COPY_IN) {
        throw fmt_error("Database error on COPY: {}", error_msg());
    }
}

void pg_conn_t::copy_send(std::string const &data,
                          std::string const &context) const
{
    assert(m_conn);

    log_sql_data("(C{}) Copy data to '{}':\n{}", m_connection_id, context,
                 data);
    int const r = PQputCopyData(m_conn.get(), data.c_str(), (int)data.size());

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

    if (data.size() < 1100) {
        log_error("Data: {}", data);
    } else {
        log_error("Data: {}\n...\n{}", std::string(data, 0, 500),
                  std::string(data, data.size() - 500));
    }

    throw std::runtime_error{"COPYing data to Postgresql."};
}

void pg_conn_t::copy_end(std::string const &context) const
{
    assert(m_conn);

    if (PQputCopyEnd(m_conn.get(), nullptr) != 1) {
        throw fmt_error("Ending COPY mode for '{}' failed: {}.", context,
                        error_msg());
    }

    pg_result_t const res{PQgetResult(m_conn.get())};
    if (res.status() != PGRES_COMMAND_OK) {
        throw fmt_error("Ending COPY mode for '{}' failed: {}.", context,
                        error_msg());
    }
}

static std::string concat_params(int num_params,
                                 char const *const *param_values)
{
    util::string_joiner_t joiner{','};

    for (int i = 0; i < num_params; ++i) {
        joiner.add(param_values[i] ? param_values[i] : "<NULL>");
    }

    return joiner();
}

pg_result_t pg_conn_t::exec_prepared_internal(char const *stmt, int num_params,
                                              char const *const *param_values,
                                              int *param_lengths,
                                              int *param_formats,
                                              int result_format) const
{
    assert(m_conn);

    if (get_logger().log_sql()) {
        log_sql("(C{}) EXECUTE {}({})", m_connection_id, stmt,
                concat_params(num_params, param_values));
    }

    pg_result_t res{PQexecPrepared(m_conn.get(), stmt, num_params, param_values,
                                   param_lengths, param_formats,
                                   result_format)};

    auto const status = res.status();
    if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
        log_error("SQL command failed: EXECUTE {}({})", stmt,
                  concat_params(num_params, param_values));
        throw fmt_error("Database error: {} ({})", error_msg(), status);
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

void check_identifier(std::string const &name, char const *in)
{
    auto const pos = name.find_first_of("\"',.;$%&/()<>{}=?^*#");

    if (pos == std::string::npos) {
        return;
    }

    throw fmt_error("Special characters are not allowed in {}: '{}'.", in,
                    name);
}
