#ifndef OSM2PGSQL_PGSQL_HPP
#define OSM2PGSQL_PGSQL_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

/**
 * \file
 *
 * This file is part of osm2pgsql (https://github.com/openstreetmap/osm2pgsql).
 *
 * Helper classes and functions for PostgreSQL access.
 */

#include "format.hpp"
#include "osmtypes.hpp"

#include <libpq-fe.h>

#include <cassert>
#include <map>
#include <memory>
#include <string>

/**
 * PostgreSQL query result.
 *
 * Wraps the PGresult object of the libpq library.
 */
class pg_result_t
{
public:
    explicit pg_result_t(PGresult *result) noexcept : m_result(result) {}

    /// Get a pointer to the underlying PGresult object.
    PGresult *get() const noexcept { return m_result.get(); }

    /// Get the status of this result.
    ExecStatusType status() const noexcept
    {
        return PQresultStatus(m_result.get());
    }

    /// The number of fields (columns) in this result.
    int num_fields() const noexcept { return PQnfields(m_result.get()); }

    /// The number of tuples (rows) in this result.
    int num_tuples() const noexcept { return PQntuples(m_result.get()); }

    /// Does the field at (row, col) has the NULL value?
    bool is_null(int row, int col) const noexcept
    {
        assert(row < num_tuples() && col < num_fields());
        return PQgetisnull(m_result.get(), row, col) != 0;
    }

    /// The length of the field at (row, col) in bytes.
    int get_length(int row, int col) const noexcept
    {
        assert(row < num_tuples() && col < num_fields());
        return PQgetlength(m_result.get(), row, col);
    }

    /**
     * Get value of the field at (row, col) as char pointer. The string is
     * null-terminated. Only valid as long as the pg_result_t is in scope.
     */
    char const *get_value(int row, int col) const noexcept
    {
        assert(row < num_tuples() && col < num_fields());
        return PQgetvalue(m_result.get(), row, col);
    }

    /**
     * Create a std::string with the value of the field at (row, col). This
     * does the correct thing for binary data.
     */
    std::string get_value_as_string(int row, int col) const noexcept
    {
        return std::string(get_value(row, col),
                           (std::size_t)get_length(row, col));
    }

    /**
     * Get the column number from the name. Returns -1 if there is no column
     * of that name.
     */
    int get_column_number(std::string const &name) const noexcept
    {
        return PQfnumber(m_result.get(), ('"' + name + '"').c_str());
    }

private:
    struct pg_result_deleter_t
    {
        void operator()(PGresult *p) const noexcept { PQclear(p); }
    };

    std::unique_ptr<PGresult, pg_result_deleter_t> m_result;
};

namespace {
/**
 * Helper for pg_conn_t::exec_prepared() function. All parameters to
 * that function are given to the exec_arg::to_str function which will
 * pass through string-like parameters and convert other parameters to
 * strings.
 */
template <typename T>
struct exec_arg
{
    constexpr static std::size_t const buffers_needed = 1;
    static char const *to_str(std::vector<std::string> *data, T param)
    {
        return data->emplace_back("{}"_format(std::forward<T>(param))).c_str();
    }
};

template <>
struct exec_arg<char const *>
{
    constexpr static std::size_t const buffers_needed = 0;
    static char const *to_str(std::vector<std::string> * /*data*/,
                              char const *param)
    {
        return param;
    }
};

template <>
struct exec_arg<std::string const &>
{
    constexpr static std::size_t const buffers_needed = 0;
    static char const *to_str(std::vector<std::string> * /*data*/,
                              std::string const &param)
    {
        return param.c_str();
    }
};

} // anonymous namespace

/**
 * PostgreSQL connection.
 *
 * Wraps the PGconn object of the libpq library.
 *
 * The connection is automatically closed when the object is destroyed or
 * you can close it explicitly by calling close().
 */
class pg_conn_t
{
public:
    explicit pg_conn_t(std::string const &conninfo);

    /**
     * Run the named prepared SQL statement and return the results.
     *
     * \param stmt The name of the prepared statement.
     * \param params Any number of arguments (will be converted to strings
     *               if necessary).
     * \throws exception if the command failed.
     */
    template <typename... TArgs>
    pg_result_t exec_prepared(char const *stmt, TArgs... params) const
    {
        // We have to convert all non-string parameters into strings and
        // store them somewhere. We use the exec_params vector for this.
        // It needs to be large enough to hold all parameters without resizing
        // so that pointers into the strings in that vector remain valid
        // after new parameters have been added.
        constexpr auto const total_buffers_needed =
            (0 + ... + exec_arg<TArgs>::buffers_needed);
        std::vector<std::string> exec_params;
        exec_params.reserve(total_buffers_needed);

        // This array holds the pointers to all parameter strings, either
        // to the original string parameters or to the recently converted
        // in the exec_params vector.
        std::array<char const *, sizeof...(params)> param_ptrs = {
            exec_arg<TArgs>::to_str(&exec_params,
                                    std::forward<TArgs>(params))...};

        return exec_prepared_internal(stmt, sizeof...(params),
                                      param_ptrs.data());
    }

    /**
     * Run the specified SQL query and return the results.
     *
     * \param expect The expected status code of the SQL command.
     * \param sql The SQL command.
     * \throws exception if the result is not as expected.
     */
    pg_result_t query(ExecStatusType expect, char const *sql) const;
    pg_result_t query(ExecStatusType expect, std::string const &sql) const;

    void set_config(char const *setting, char const *value) const;

    /**
     * Run the specified SQL query. This can only be used for commands that
     * have no output and return status code PGRES_COMMAND_OK.
     *
     * \param sql The SQL command.
     * \throws exception if the command failed.
     */
    void exec(char const *sql) const;
    void exec(std::string const &sql) const;

    void copy_data(std::string const &sql, std::string const &context) const;

    void end_copy(std::string const &context) const;

    char const *error_msg() const noexcept;

    /// Close database connection.
    void close() noexcept { m_conn.reset(); }

private:
    pg_result_t exec_prepared_internal(char const *stmt, int num_params,
                                       char const *const *param_values) const;

    struct pg_conn_deleter_t
    {
        void operator()(PGconn *p) const noexcept { PQfinish(p); }
    };

    std::unique_ptr<PGconn, pg_conn_deleter_t> m_conn;
};

/**
 * Return a TABLESPACE clause with the specified tablespace name or an empty
 * string if the name is empty.
 */
std::string tablespace_clause(std::string const &name);

/**
 * Return the possibly schema-qualified name of a table. Names are enclosed
 * in double quotes.
 */
std::string qualified_name(std::string const &schema, std::string const &name);

struct postgis_version
{
    int major;
    int minor;
};

/// Get all config settings from the database.
std::map<std::string, std::string>
get_postgresql_settings(pg_conn_t const &db_connection);

/// Get PostGIS major and minor version.
postgis_version get_postgis_version(pg_conn_t const &db_connection);

#endif // OSM2PGSQL_PGSQL_HPP
