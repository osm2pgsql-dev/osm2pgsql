/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2026 by the osm2pgsql developer community.
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
#include <type_traits>
#include <utility>

std::size_t pg_result_t::affected_rows() const noexcept
{
    char const *const rows_as_string = PQcmdTuples(m_result.get());
    return std::strtoull(rows_as_string, nullptr, 10);
}

std::atomic<std::uint32_t> pg_conn_t::connection_id{0};

namespace {

PGconn *open_connection(connection_params_t const &connection_params,
                        std::string_view context, std::uint32_t id)
{
    std::vector<char const *> keywords;
    std::vector<char const *> values;

    for (auto const &[k, v] : connection_params) {
        keywords.push_back(k.c_str());
        values.push_back(v.c_str());
    }

    std::string const app_name{fmt::format("osm2pgsql.{}/C{}", context, id)};
    keywords.push_back("fallback_application_name");
    values.push_back(app_name.c_str());

    keywords.push_back(nullptr);
    values.push_back(nullptr);

    return PQconnectdbParams(keywords.data(), values.data(), 1);
}

std::string concat_params(int num_params, char const *const *param_values)
{
    util::string_joiner_t joiner{','};

    for (int i = 0; i < num_params; ++i) {
        joiner.add(param_values[i] ? param_values[i] : "<NULL>");
    }

    return joiner();
}

} // anonymous namespace

pg_conn_t::pg_conn_t(connection_params_t const &connection_params,
                     std::string_view context)
: m_connection_id(connection_id.fetch_add(1))
{
    m_conn.reset(open_connection(connection_params, context, m_connection_id));

    if (!m_conn) {
        throw fmt_error("Connecting to database failed (context={}).", context);
    }

    if (PQstatus(m_conn.get()) != CONNECTION_OK) {
        throw fmt_error("Connecting to database failed (context={}): {}.",
                        context, error_msg());
    }

    if (get_logger().log_sql()) {
        auto const results = exec("SELECT pg_backend_pid()");
        log_sql("(C{}) New database connection (context={}, backend_pid={})",
                m_connection_id, context, results.get(0, 0));
    }

    // PostgreSQL sends notices in many different contexts which aren't that
    // useful for the user. So we disable them for all connections.
    if (!get_logger().debug_enabled()) {
        exec("SET client_min_messages = WARNING");
    }

    // Disable synchronous_commit on all connections. For some connections it
    // might not matter, especially if they read only, but then it doesn't
    // hurt either.
    exec("SET synchronous_commit = off");
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
    exec("UPDATE pg_catalog.pg_settings SET setting = '{}' WHERE name = '{}'",
         value, setting);
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

void pg_conn_t::copy_start(std::string const &sql) const
{
    assert(m_conn);

    log_sql("(C{}) {}", m_connection_id, sql);
    pg_result_t const res{PQexec(m_conn.get(), sql.c_str())};
    if (res.status() != PGRES_COPY_IN) {
        throw fmt_error("Database error on COPY: {}", error_msg());
    }
}

void pg_conn_t::copy_send(std::string_view data, std::string_view context) const
{
    assert(m_conn);

    log_sql_data("(C{}) Copy data to '{}':\n{}", m_connection_id, context,
                 data);
    int const result =
        PQputCopyData(m_conn.get(), data.data(), (int)data.size());

    switch (result) {
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
        log_error("Data: {}\n...\n{}", data.substr(0, 500),
                  data.substr(data.size() - 500, 500));
    }

    throw std::runtime_error{"COPYing data to Postgresql."};
}

void pg_conn_t::copy_end(std::string_view context) const
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

void pg_conn_t::prepare_internal(std::string const &stmt,
                                 std::string const &sql) const
{
    if (get_logger().log_sql()) {
        log_sql("(C{}) PREPARE {} AS {}", m_connection_id, stmt, sql);
    }

    pg_result_t const res{
        PQprepare(m_conn.get(), stmt.c_str(), sql.c_str(), 0, nullptr)};
    if (res.status() != PGRES_COMMAND_OK) {
        throw fmt_error("Prepare failed for '{}': {}.", sql, error_msg());
    }
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
        throw fmt_error("Database error: {} ({})", error_msg(),
                        std::underlying_type_t<ExecStatusType>(status));
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
    assert(!schema.empty());
    return fmt::format(R"("{}"."{}")", schema, name);
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
