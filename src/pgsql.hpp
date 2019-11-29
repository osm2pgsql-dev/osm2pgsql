#ifndef OSM2PGSQL_PGSQL_HPP
#define OSM2PGSQL_PGSQL_HPP

/* Helper functions for PostgreSQL access */

#include <cstring>
#include <libpq-fe.h>
#include <memory>
#include <string>

struct pg_result_deleter_t
{
    void operator()(PGresult *p) const noexcept { PQclear(p); }
};

using pg_result_t = std::unique_ptr<PGresult, pg_result_deleter_t>;

/**
 * Simple postgres connection.
 *
 * The connection is automatically closed when the object is destroyed.
 */
class pg_conn_t
{
public:
    pg_conn_t(std::string const &connection);

    pg_conn_t(pg_conn_t const &) = delete;
    pg_conn_t &operator=(const pg_conn_t &) = delete;

    ~pg_conn_t();

    pg_result_t exec_prepared(char const *stmt, int num_params,
                              const char *const *param_values,
                              ExecStatusType expect = PGRES_TUPLES_OK) const;

    void copy_data(std::string const &sql, std::string const &context) const;
    void end_copy(std::string const &context) const;

    pg_result_t query(ExecStatusType expect, char const *sql) const;

    pg_result_t query(ExecStatusType expect, std::string const &sql) const;

    void exec(char const *sql) const;

    void exec(std::string const &sql) const;

private:
    PGconn *m_conn;
};

#endif // OSM2PGSQL_PGSQL_HPP
