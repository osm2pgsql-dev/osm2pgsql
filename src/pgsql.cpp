/* Helper functions for the postgresql connections */
#include "format.hpp"
#include "pgsql.hpp"
#include "util.hpp"

#include <array>
#include <cstdarg>
#include <cstdio>

pg_conn_t::pg_conn_t(std::string const &conninfo)
: m_conn(PQconnectdb(conninfo.c_str()))
{
    if (PQstatus(m_conn.get()) != CONNECTION_OK) {
        fmt::print(stderr, "Connection to database failed: {}\n", error_msg());
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
    fmt::print(stderr, "Executing: {}\n", sql);
#endif
    pg_result_t res{PQexec(m_conn.get(), sql)};
    if (PQresultStatus(res.get()) != expect) {
        fmt::print(stderr, "SQL command failed: {}\nFull query: {}\n",
                   error_msg(), sql);
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
    fmt::print(stderr, "{}>>> {}\n", context, sql);
#endif
    int const r = PQputCopyData(m_conn.get(), sql.c_str(), (int)sql.size());

    if (r == 1) {
        return; // success
    }

    switch (r) {
    case 0: // need to wait for write ready
        fmt::print(stderr, "{} - COPY unexpectedly busy\n", context);
        break;
    case -1: // error occurred
        fmt::print(stderr, "{} - error on COPY: {}\n", context, error_msg());
        break;
    }

    if (sql.size() < 1100) {
        fmt::print(stderr, "Data:\n{}\n", sql);
    } else {
        fmt::print(stderr, "Data:\n{}\n...\n{}\n", std::string(sql, 0, 500),
                   std::string(sql, sql.size() - 500));
    }

    throw std::runtime_error{"COPYing data to Postgresql."};
}

void pg_conn_t::end_copy(std::string const &context) const
{
    if (PQputCopyEnd(m_conn.get(), nullptr) != 1) {
        fmt::print(stderr, "COPY END for {} failed: {}\n", context,
                   error_msg());
        throw std::runtime_error{"Ending COPY mode"};
    }

    pg_result_t const res{PQgetResult(m_conn.get())};
    if (PQresultStatus(res.get()) != PGRES_COMMAND_OK) {
        fmt::print(stderr, "result COPY END for {} failed: {}\n", context,
                   error_msg());
        throw std::runtime_error{"Ending COPY mode"};
    }
}

pg_result_t
pg_conn_t::exec_prepared_internal(char const *stmt, int num_params,
                                  char const *const *param_values) const
{
#ifdef DEBUG_PGSQL
    fmt::print(stderr, "ExecPrepared: {}\n", stmt);
#endif
    pg_result_t res{PQexecPrepared(m_conn.get(), stmt, num_params, param_values,
                                   nullptr, nullptr, 0)};
    if (PQresultStatus(res.get()) != PGRES_TUPLES_OK) {
        fmt::print(stderr, "Prepared statement failed with: {} ({})\n",
                   error_msg(), PQresultStatus(res.get()));
        fmt::print(stderr, "Query: {}\n", stmt);
        if (num_params) {
            fmt::print(stderr, "with arguments:\n");
            for (int i = 0; i < num_params; ++i) {
                fmt::print(stderr, "  {}: {}\n, ", i,
                           param_values[i] ? param_values[i] : "<NULL>");
            }
        }
        throw std::runtime_error{"Executing prepared statement"};
    }

    return res;
}

pg_result_t pg_conn_t::exec_prepared(char const *stmt, char const *p1, char const *p2) const
{
    std::array<const char *, 2> params{p1, p2};
    return exec_prepared_internal(stmt, params.size(), params.data());
}

pg_result_t pg_conn_t::exec_prepared(char const *stmt, char const *param) const
{
    return exec_prepared_internal(stmt, 1, &param);
}

pg_result_t pg_conn_t::exec_prepared(char const *stmt,
                                     std::string const &param) const
{
    return exec_prepared(stmt, param.c_str());
}

pg_result_t pg_conn_t::exec_prepared(char const *stmt, osmid_t id) const
{
    util::integer_to_buffer buffer{id};
    return exec_prepared(stmt, buffer.c_str());
}

std::string tablespace_clause(std::string const &name)
{
    std::string sql;

    if (!name.empty()) {
        sql += " TABLESPACE ";
        sql += name;
    }

    return sql;
}

