/* Helper functions for the postgresql connections */
#include "pgsql.hpp"

#include <boost/format.hpp>
#include <cstdarg>
#include <cstdio>

pg_conn_t::pg_conn_t(std::string const &connection)
{
    m_conn = PQconnectdb(connection.c_str());
    if (PQstatus(m_conn) != CONNECTION_OK) {
        fprintf(stderr, "Connection to database failed: %s\n",
                PQerrorMessage(m_conn));
        throw std::runtime_error("Connecting to database.");
    }
}

pg_conn_t::~pg_conn_t()
{
    if (m_conn) {
        PQfinish(m_conn);
    }
}

pg_result_t pg_conn_t::query(ExecStatusType expect,
                             std::string const &sql) const
{
#ifdef DEBUG_PGSQL
    fprintf(stderr, "Executing: %s\n", sql.c_str());
#endif
    pg_result_t res(PQexec(m_conn, sql.c_str()));
    if (PQresultStatus(res.get()) != expect) {
        fprintf(stderr, "Unexpected result for SQL query: %s\nFull query: %s\n",
                PQerrorMessage(m_conn), sql.c_str());
        throw std::runtime_error("Executing SQL");
    }
    return res;
}

void pg_conn_t::exec(char const *sql) const
{
#ifdef DEBUG_PGSQL
    fprintf(stderr, "Executing: %s\n", sql);
#endif
    pg_result_t res(PQexec(m_conn, sql));
    if (PQresultStatus(res.get()) != PGRES_COMMAND_OK) {
        fprintf(stderr, "SQL command failed: %s\nFull query: %s\n",
                PQerrorMessage(m_conn), sql);
        throw std::runtime_error("Executing SQL command.");
    }
}

void pg_conn_t::copy_data(std::string const &sql,
                          std::string const &context) const
{
#ifdef DEBUG_PGSQL
    fprintf(stderr, "%s>>> %s\n", context.c_str(), sql.c_str());
#endif
    int r = PQputCopyData(m_conn, sql.c_str(), (int)sql.size());

    if (r != 1) {
        switch (r) {
        case 0: // need to wait for write ready
            fprintf(stderr, "%s - COPY unexpectedly busy\n", context.c_str());
            break;
        case -1: // error occurred
            fprintf(stderr, "%s - error on COPY: %s\n", context.c_str(),
                    PQerrorMessage(m_conn));
            break;
            // other possibility is 1 which means success
        }
        if (sql.size() < 1100) {
            fprintf(stderr, "Data:\n%s\n", sql.c_str());
        } else {
            fprintf(stderr, "Data:\n%s\n...\n%s\n",
                    std::string(sql, 0, 500).c_str(),
                    std::string(sql, sql.size() - 500).c_str());
        }
        throw std::runtime_error("COPYing data to Postgresql.");
    }
}

void pg_conn_t::end_copy(std::string const &context) const
{
    if (PQputCopyEnd(m_conn, nullptr) != 1) {
        fprintf(stderr, "COPY END for %s failed: %s\n", context.c_str(),
                PQerrorMessage(m_conn));
        throw std::runtime_error("Ending COPY mode");
    }

    pg_result_t res(PQgetResult(m_conn));
    if (PQresultStatus(res.get()) != PGRES_COMMAND_OK) {
        fprintf(stderr, "result COPY END for %s failed: %s\n", context.c_str(),
                PQerrorMessage(m_conn));
        throw std::runtime_error("Ending COPY mode");
    }
}

pg_result_t pg_conn_t::exec_prepared(char const *stmtName, int nParams,
                                     const char *const *paramValues,
                                     const ExecStatusType expect) const
{
#ifdef DEBUG_PGSQL
    fprintf(stderr, "ExecPrepared: %s\n", stmtName);
#endif
    //run the prepared statement
    pg_result_t res(PQexecPrepared(m_conn, stmtName, nParams, paramValues,
                                   nullptr, nullptr, 0));
    if (PQresultStatus(res.get()) != expect) {
        fprintf(stderr, "Prepared statement failed with: %s (%d)\n",
                PQerrorMessage(m_conn), PQresultStatus(res.get()));
        fprintf(stderr, "Query: %s\n", stmtName);
        if (nParams) {
            fprintf(stderr, "with arguments:\n");
            for (int i = 0; i < nParams; i++) {
                fprintf(stderr, "  %d: %s\n, ", i,
                        paramValues[i] ? paramValues[i] : "<NULL>");
            }
        }
        throw std::runtime_error("Executing prepared statement");
    }

    return res;
}
