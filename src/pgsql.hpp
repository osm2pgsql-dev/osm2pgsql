#ifndef OSM2PGSQL_PGSQL_HPP
#define OSM2PGSQL_PGSQL_HPP

/**
 * \file
 *
 * This file is part of osm2pgsql (https://github.com/openstreetmap/osm2pgsql).
 *
 * Helper classes and functions for PostgreSQL access.
 */

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

    /// Execute a prepared statement with one parameter.
    pg_result_t exec_prepared(char const *stmt, char const *param) const;

    /// Execute a prepared statement with two parameters.
    pg_result_t exec_prepared(char const *stmt, char const *p1, char const *p2) const;

    /// Execute a prepared statement with one string parameter.
    pg_result_t exec_prepared(char const *stmt, std::string const &param) const;

    /// Execute a prepared statement with one integer parameter.
    pg_result_t exec_prepared(char const *stmt, osmid_t id) const;

    pg_result_t query(ExecStatusType expect, char const *sql) const;

    pg_result_t query(ExecStatusType expect, std::string const &sql) const;

    void set_config(char const *setting, char const *value) const;

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
