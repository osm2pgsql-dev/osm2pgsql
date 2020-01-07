#ifndef OSM2PGSQL_PGSQL_HPP
#define OSM2PGSQL_PGSQL_HPP

/* Helper functions for PostgreSQL access */

#include <libpq-fe.h>

#include <cassert>
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
        return std::string(get_value(row, col), get_length(row, col));
    }

    /**
     * Get the column number from the name. Returns -1 if there is no column
     * of that name.
     */
    int get_column_number(std::string const& name) const noexcept
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
 * The connection is automatically closed when the object is destroyed.
 */
class pg_conn_t
{
public:
    explicit pg_conn_t(std::string const &conninfo);

    pg_result_t exec_prepared(char const *stmt, int num_params,
                              const char *const *param_values,
                              ExecStatusType expect = PGRES_TUPLES_OK) const;

    pg_result_t query(ExecStatusType expect, char const *sql) const;

    pg_result_t query(ExecStatusType expect, std::string const &sql) const;

    void exec(char const *sql) const;

    void exec(std::string const &sql) const;

    void copy_data(std::string const &sql, std::string const &context) const;

    void end_copy(std::string const &context) const;

    char const *error_msg() const noexcept;

private:
    struct pg_conn_deleter_t
    {
        void operator()(PGconn *p) const noexcept { PQfinish(p); }
    };

    std::unique_ptr<PGconn, pg_conn_deleter_t> m_conn;
};

#endif // OSM2PGSQL_PGSQL_HPP
