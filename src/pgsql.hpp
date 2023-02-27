#ifndef OSM2PGSQL_PGSQL_HPP
#define OSM2PGSQL_PGSQL_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
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

#include <libpq-fe.h>

#include <array>
#include <atomic>
#include <cassert>
#include <map>
#include <memory>
#include <string>
#include <vector>

/**
 * PostgreSQL query result.
 *
 * Wraps the PGresult object of the libpq library.
 */
class pg_result_t
{
public:
    pg_result_t() noexcept = default;

    explicit pg_result_t(PGresult *result) noexcept : m_result(result) {}

    /// Get the status of this result.
    ExecStatusType status() const noexcept
    {
        return PQresultStatus(m_result.get());
    }

    /// The number of fields (columns) in this result.
    int num_fields() const noexcept { return PQnfields(m_result.get()); }

    /// The number of tuples (rows) in this result.
    int num_tuples() const noexcept { return PQntuples(m_result.get()); }

    /**
     * Does the field at (row, col) has the NULL value?
     *
     * \pre 0 <= row < num_tuples() and 0 <= col < num_fields()
     */
    bool is_null(int row, int col) const noexcept
    {
        assert(row >= 0 && row < num_tuples() && col >= 0 &&
               col < num_fields());
        return PQgetisnull(m_result.get(), row, col) != 0;
    }

    /// Return the number of INSERTed, UPDATEd, or DELETEed rows.
    std::size_t affected_rows() const noexcept;

    /**
     * The length of the field at (row, col) in bytes.
     *
     * \pre 0 <= row < num_tuples() and 0 <= col < num_fields()
     */
    int get_length(int row, int col) const noexcept
    {
        assert(row >= 0 && row < num_tuples() && col >= 0 &&
               col < num_fields());
        return PQgetlength(m_result.get(), row, col);
    }

    /**
     * Get value of the field at (row, col) as char pointer. The string is
     * null-terminated. Only valid as long as the pg_result_t is in scope.
     *
     * When the result is NULL, an empty string is returned.
     *
     * \pre 0 <= row < num_tuples() and 0 <= col < num_fields()
     */
    char const *get_value(int row, int col) const noexcept
    {
        assert(row >= 0 && row < num_tuples() && col >= 0 &&
               col < num_fields());
        return PQgetvalue(m_result.get(), row, col);
    }

    /**
     * Return a std::string_view with the value of the field at (row, col).
     */
    std::string_view get(int row, int col) const noexcept
    {
        return {get_value(row, col),
                static_cast<std::size_t>(get_length(row, col))};
    }

    /**
     * Get the column number from the name. Returns -1 if there is no column
     * of that name.
     */
    int get_column_number(std::string const &name) const noexcept
    {
        return PQfnumber(m_result.get(), ('"' + name + '"').c_str());
    }

    /// Return true if this holds an actual result.
    explicit operator bool() const noexcept { return m_result.get(); }

private:
    struct pg_result_deleter_t
    {
        void operator()(PGresult *p) const noexcept { PQclear(p); }
    };

    std::unique_ptr<PGresult, pg_result_deleter_t> m_result;
};

/**
 * Wrapper class for query parameters that should be sent to the database
 * as binary parameter.
 */
class binary_param : public std::string_view
{
public:
    using std::string_view::string_view;

    binary_param(std::string const &str)
    : std::string_view(str.data(), str.size())
    {}
};

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
     * Run the specified SQL command.
     *
     * \param sql The SQL command.
     * \throws std::runtime_exception If the command failed (didn't return
     *         status code PGRES_COMMAND_OK or PGRES_TUPLES_OK).
     */
    pg_result_t exec(char const *sql) const;
    pg_result_t exec(std::string const &sql) const;

    /**
     * Run the specified SQL command.
     *
     * \param sql The SQL command using fmt lib patterns.
     * \param params Any number of arguments for the fmt lib.
     * \throws std::runtime_exception If the command failed (didn't return
     *         status code PGRES_COMMAND_OK or PGRES_TUPLES_OK).
     */
    template <typename... TArgs>
    pg_result_t exec(char const *sql, TArgs... params) const
    {
        return exec(fmt::format(sql, std::forward<TArgs>(params)...));
    }

    /**
     * Run the named prepared SQL statement and return the results in text
     * format.
     *
     * \param stmt The name of the prepared statement.
     * \param params Any number of arguments (will be converted to strings
     *               if necessary).
     * \throws exception if the command failed.
     */
    template <typename... TArgs>
    pg_result_t exec_prepared(char const *stmt, TArgs... params) const
    {
        return exec_prepared_with_result_format(stmt, false,
                                                std::forward<TArgs>(params)...);
    }

    /**
     * Run the named prepared SQL statement and return the results in binary
     * format.
     *
     * \param stmt The name of the prepared statement.
     * \param params Any number of arguments (will be converted to strings
     *               if necessary).
     * \throws exception if the command failed.
     */
    template <typename... TArgs>
    pg_result_t exec_prepared_as_binary(char const *stmt, TArgs... params) const
    {
        return exec_prepared_with_result_format(stmt, true,
                                                std::forward<TArgs>(params)...);
    }

    /**
     * Update a PostgreSQL setting (like with the SET command). Will silently
     * ignore settings that are not available or any other errors.
     */
    void set_config(char const *setting, char const *value) const;

    void copy_start(char const *sql) const;
    void copy_send(std::string const &data, std::string const &context) const;
    void copy_end(std::string const &context) const;

    /// Return the latest generated error message on this connection.
    char const *error_msg() const noexcept;

    /// Close database connection.
    void close();

private:
    pg_result_t exec_prepared_internal(char const *stmt, int num_params,
                                       char const *const *param_values,
                                       int *param_lengths, int *param_formats,
                                       int result_format) const;

    /**
     * Helper for pg_conn_t::exec_prepared_with_result_format() function. Used
     * to find out how many buffers we need. Must always be in sync with the
     * to_str() function below.
     */
    template <typename T>
    static constexpr std::size_t buffers_needed() noexcept
    {
        if constexpr (std::is_same_v<T, char const *>) {
            return 0;
        } else if constexpr (std::is_same_v<T, std::string>) {
            return 0;
        } else if constexpr (std::is_same_v<T, binary_param>) {
            return 0;
        }
        return 1;
    }

    /**
     * Helper for pg_conn_t::exec_prepared_with_result_format() function. All
     * parameters to that function are given to the to_str() function which
     * will pass through string-like parameters and convert other parameters to
     * strings.
     */
    template <typename T>
    static char const *to_str(std::vector<std::string> *data, int *length,
                              int *bin, T const &param)
    {
        if constexpr (std::is_same_v<T, char const *>) {
            return param;
        } else if constexpr (std::is_same_v<T, std::string>) {
            *length = param.size();
            return param.c_str();
        } else if constexpr (std::is_same_v<T, binary_param>) {
            *length = param.size();
            *bin = 1;
            return param.data();
        }
        return data->emplace_back(fmt::to_string(param)).c_str();
    }

    /**
     * Run the named prepared SQL statement and return the results.
     *
     * \param stmt The name of the prepared statement.
     * \param result_as_binary Ask for the resuls to be returned in binary
     *                         format.
     * \param params Any number of arguments (will be converted to strings
     *               if necessary).
     * \throws exception if the command failed.
     */
    template <typename... TArgs>
    pg_result_t exec_prepared_with_result_format(char const *stmt,
                                                 bool result_as_binary,
                                                 TArgs... params) const
    {
        // We have to convert all non-string parameters into strings and
        // store them somewhere. We use the exec_params vector for this.
        // It needs to be large enough to hold all parameters without resizing
        // so that pointers into the strings in that vector remain valid
        // after new parameters have been added.
        constexpr auto const total_buffers_needed =
            (0 + ... + buffers_needed<std::decay_t<TArgs>>());
        std::vector<std::string> exec_params;
        exec_params.reserve(total_buffers_needed);

        std::array<int, sizeof...(params)> lengths = {0};
        std::array<int, sizeof...(params)> bins = {0};

        // This array holds the pointers to all parameter strings, either
        // to the original string parameters or to the recently converted
        // in the exec_params vector.
        std::size_t n = 0;
        std::size_t m = 0;
        std::array<char const *, sizeof...(params)> param_ptrs = {
            to_str<std::decay_t<TArgs>>(&exec_params, &lengths[n++], &bins[m++],
                                        std::forward<TArgs>(params))...};

        return exec_prepared_internal(stmt, sizeof...(params),
                                      param_ptrs.data(), lengths.data(),
                                      bins.data(), result_as_binary ? 1 : 0);
    }

    struct pg_conn_deleter_t
    {
        void operator()(PGconn *p) const noexcept { PQfinish(p); }
    };

    std::unique_ptr<PGconn, pg_conn_deleter_t> m_conn;

    // Used to generate unique ids for each database connection.
    static std::atomic<std::uint32_t> connection_id;

    // The unique id of this database connection.
    std::uint32_t m_connection_id;
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

/**
 * Check that the string conforms to the identifier syntax we accept.
 *
 * Note that PostgreSQL accepts any character in a quoted identifier.
 * This function checks for some characters that are potentially problematic
 * in our internal functions that create SQL statements.
 *
 * \param name Identifier to check.
 * \param in Name of the identifier. Only used to create a human-readable error.
 * \throws runtime_exception If an invalid character is found in the name.
 */
void check_identifier(std::string const &name, char const *in);

#endif // OSM2PGSQL_PGSQL_HPP
