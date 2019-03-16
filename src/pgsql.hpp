#ifndef OSM2PGSQL_PGSQL_HPP
#define OSM2PGSQL_PGSQL_HPP

/* Helper functions for pgsql access */

/* Current middle and output-pgsql do a lot of things similarly, this should
 * be used to abstract to commonalities */

#include <boost/format.hpp>
#include <cstring>
#include <libpq-fe.h>
#include <memory>
#include <string>

struct pg_result_deleter_t
{
    void operator()(PGresult *p) const { PQclear(p); }
};

typedef std::unique_ptr<PGresult, pg_result_deleter_t> pg_result_t;

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

    pg_result_t exec_prepared(char const *stmtName, int nParams,
                              const char *const *paramValues,
                              ExecStatusType expect = PGRES_TUPLES_OK) const;
    void copy_data(std::string const &sql, std::string const &context) const;
    void end_copy(std::string const &context) const;

    pg_result_t query(ExecStatusType expect, std::string const &sql) const;

    template <typename... ARGS>
    pg_result_t query(ExecStatusType expect, std::string const &fmt,
                      ARGS &&... args) const
    {
        boost::format formatter(fmt);
        format_sql(formatter, args...);
        return query(expect, fmt, formatter.str());
    }

    void exec(char const *sql) const;

    template <typename... ARGS>
    void exec(char const *fmt, ARGS &&... args) const
    {
        boost::format formatter(fmt);
        format_sql(formatter, args...);

        exec(formatter.str().c_str());
    }

private:
    template <typename T, typename... ARGS>
    void format_sql(boost::format &fmt, T arg, ARGS &&... args) const
    {
        fmt % arg;
        format_sql(fmt, args...);
    }

    template <typename T>
    void format_sql(boost::format &fmt, T arg) const
    {
        fmt % arg;
    }

    PGconn *m_conn;
};

#endif // OSM2PGSQL_PGSQL_HPP
