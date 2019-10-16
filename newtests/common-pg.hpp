#ifndef OSM2PGSQL_TEST_COMMON_PG_HPP
#define OSM2PGSQL_TEST_COMMON_PG_HPP

#include <cstdio>
#include <stdexcept>
#include <string>

#include <libpq-fe.h>

#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>

#include "catch.hpp"
#include "options.hpp"

/// Helper classes for postgres connections
namespace pg
{

class result_t
{
public:
    result_t(PGresult *result) : m_result(result) {}

    ~result_t() { PQclear(m_result); }

    ExecStatusType status() const { return PQresultStatus(m_result); }
    int num_tuples() const { return PQntuples(m_result); }
    std::string get_value(int row, int col) const
    {
        return PQgetvalue(m_result, row, col);
    }

    bool is_null(int row, int col) const
    {
        return PQgetisnull(m_result, row, col);
    }

private:
    PGresult *m_result;
};


class conn_t
{
public:
    conn_t(char const *conninfo)
    {
        m_conn = PQconnectdb(conninfo);

        if (PQstatus(m_conn) != CONNECTION_OK) {
            fprintf(stderr, "Could not connect to database '%s' because: %s\n",
                    conninfo, PQerrorMessage(m_conn));
            PQfinish(m_conn);
            throw std::runtime_error("Database connection failed");
        }
    }

    ~conn_t()
    {
        if (m_conn) {
            PQfinish(m_conn);
        }
    }

    void exec(boost::format const &cmd, ExecStatusType expect = PGRES_COMMAND_OK)
    {
        exec(cmd.str(), expect);
    }

    void exec(std::string const &cmd, ExecStatusType expect = PGRES_COMMAND_OK)
    {
        result_t res = query(cmd);
        if (res.status() != expect) {
            fprintf(stderr, "Query '%s' failed with: %s\n", cmd.c_str(), PQerrorMessage(m_conn));
            throw std::runtime_error("DB exec failed.");
        }
    }

    result_t query(std::string const &cmd) const
    {
        return PQexec(m_conn, cmd.c_str());
    }

    result_t query(boost::format const &fmt) const { return query(fmt.str()); }

    template <typename T>
    T require_scalar(std::string const &cmd) const
    {
        result_t res = query(cmd);
        REQUIRE(res.status() == PGRES_TUPLES_OK);
        REQUIRE(res.num_tuples() == 1);

        std::string str = res.get_value(0, 0);
        return boost::lexical_cast<T>(str);
    }

    void assert_double(double expected, std::string const &cmd) const
    { REQUIRE(Approx(expected) == require_scalar<double>(cmd)); }

    result_t require_row(std::string const &cmd) const
    {
        result_t res = query(cmd);
        REQUIRE(res.status() == PGRES_TUPLES_OK);
        REQUIRE(res.num_tuples() == 1);

        return res;
    }

    unsigned long get_count(char const *table_name, std::string const &where = "") const
    {
        auto query = boost::format("select count(*) from %1% %2% %3%")
                                   % table_name
                                   % (where.empty() ? "" : "where")
                                   % where;

        return require_scalar<unsigned long>(query.str());
    }

    void require_has_table(char const *table_name) const
    {
        auto where = boost::format("oid = '%1%'::regclass") % table_name;

        REQUIRE(get_count("pg_catalog.pg_class", where.str()) == 1);
    }

private:
    PGconn *m_conn = nullptr;
};


class tempdb_t
{
public:
    tempdb_t()
    {
        conn_t conn("dbname=postgres");

        m_db_name = (boost::format("osm2pgsql-test-%1%-%2%") % getpid() % time(nullptr)).str();
        conn.exec(boost::format("DROP DATABASE IF EXISTS \"%1%\"") % m_db_name);
        conn.exec(boost::format("CREATE DATABASE \"%1%\" WITH ENCODING 'UTF8'") % m_db_name);

        conn_t local = connect();
        local.exec("CREATE EXTENSION postgis");
        local.exec("CREATE EXTENSION hstore");
    }

    ~tempdb_t()
    {
        if (!m_db_name.empty()) {
            conn_t conn("dbname=postgres");
            conn.query(boost::format("DROP DATABASE IF EXISTS \"%1%\"") %
                       m_db_name);
        }
    }

    conn_t connect() const
    {
        return conn_t(conninfo().c_str());
    }

    std::string conninfo() const { return "dbname=" + m_db_name; }

    database_options_t db_options() const
    {
        database_options_t opt;
        opt.db = m_db_name;

        return opt;
    }

private:
    std::string m_db_name;
};

}

#endif // OSM2PGSQL_TEST_COMMON_PG_HPP
