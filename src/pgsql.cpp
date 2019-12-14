/* Helper functions for the postgresql connections */
#include "pgsql.hpp"

#include <cstdarg>
#include <cstdio>

pg_conn_t::pg_conn_t(std::string const &conninfo)
: m_conn(PQconnectdb(conninfo.c_str()))
{
    if (PQstatus(m_conn.get()) != CONNECTION_OK) {
        fprintf(stderr, "Connection to database failed: %s\n", error_msg());
        throw std::runtime_error{"Connecting to database."};
    }
}

char const *pg_conn_t::error_msg() const noexcept
{
    return PQerrorMessage(m_conn.get());
}

pg_result_t pg_conn_t::query(ExecStatusType expect, char const *sql) const
{
#ifdef DEBUG_PGSQL
    fprintf(stderr, "Executing: %s\n", sql);
#endif
    pg_result_t res{PQexec(m_conn.get(), sql)};
    if (PQresultStatus(res.get()) != expect) {
        fprintf(stderr, "SQL command failed: %s\nFull query: %s\n", error_msg(),
                sql);
        throw std::runtime_error{"Executing SQL"};
    }
    return res;
}

pg_result_t pg_conn_t::query(ExecStatusType expect,
                             std::string const &sql) const
{
    return query(expect, sql.c_str());
}

void pg_conn_t::exec(char const *sql) const { query(PGRES_COMMAND_OK, sql); }

void pg_conn_t::exec(std::string const &sql) const
{
    query(PGRES_COMMAND_OK, sql.c_str());
}

void pg_conn_t::copy_data(std::string const &sql,
                          std::string const &context) const
{
#ifdef DEBUG_PGSQL
    fprintf(stderr, "%s>>> %s\n", context.c_str(), sql.c_str());
#endif
    int const r = PQputCopyData(m_conn.get(), sql.c_str(), (int)sql.size());

    if (r == 1) {
        return; // success
    }

    switch (r) {
    case 0: // need to wait for write ready
        fprintf(stderr, "%s - COPY unexpectedly busy\n", context.c_str());
        break;
    case -1: // error occurred
        fprintf(stderr, "%s - error on COPY: %s\n", context.c_str(),
                error_msg());
        break;
    }

    if (sql.size() < 1100) {
        fprintf(stderr, "Data:\n%s\n", sql.c_str());
    } else {
        fprintf(stderr, "Data:\n%s\n...\n%s\n",
                std::string(sql, 0, 500).c_str(),
                std::string(sql, sql.size() - 500).c_str());
    }

    throw std::runtime_error{"COPYing data to Postgresql."};
}

void pg_conn_t::end_copy(std::string const &context) const
{
    if (PQputCopyEnd(m_conn.get(), nullptr) != 1) {
        fprintf(stderr, "COPY END for %s failed: %s\n", context.c_str(),
                error_msg());
        throw std::runtime_error{"Ending COPY mode"};
    }

    pg_result_t const res{PQgetResult(m_conn.get())};
    if (PQresultStatus(res.get()) != PGRES_COMMAND_OK) {
        fprintf(stderr, "result COPY END for %s failed: %s\n", context.c_str(),
                error_msg());
        throw std::runtime_error{"Ending COPY mode"};
    }
}

pg_result_t pg_conn_t::exec_prepared(char const *stmt, int num_params,
                                     char const *const *param_values,
                                     const ExecStatusType expect) const
{
#ifdef DEBUG_PGSQL
    fprintf(stderr, "ExecPrepared: %s\n", stmt);
#endif
    //run the prepared statement
    pg_result_t res{PQexecPrepared(m_conn.get(), stmt, num_params, param_values,
                                   nullptr, nullptr, 0)};
    if (PQresultStatus(res.get()) != expect) {
        fprintf(stderr, "Prepared statement failed with: %s (%d)\n",
                error_msg(), PQresultStatus(res.get()));
        fprintf(stderr, "Query: %s\n", stmt);
        if (num_params) {
            fprintf(stderr, "with arguments:\n");
            for (int i = 0; i < num_params; ++i) {
                fprintf(stderr, "  %d: %s\n, ", i,
                        param_values[i] ? param_values[i] : "<NULL>");
            }
        }
        throw std::runtime_error{"Executing prepared statement"};
    }

    return res;
}
