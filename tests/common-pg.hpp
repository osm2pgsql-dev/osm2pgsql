#ifndef OSM2PGSQL_TESTS_COMMON_PG_HPP
#define OSM2PGSQL_TESTS_COMMON_PG_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>

#include "format.hpp"
#include "options.hpp"
#include "pgsql.hpp"
#include <catch.hpp>

#ifdef _MSC_VER
#include <process.h>
#include <windows.h>
#define getpid _getpid
#else
#include <sys/types.h>
#include <unistd.h>
#endif

namespace testing {
/// Helper classes for postgres connections
namespace pg {

class conn_t : public pg_conn_t
{
public:
    conn_t(std::string const &conninfo) : pg_conn_t(conninfo) {}

    std::string result_as_string(std::string const &cmd) const
    {
        pg_result_t const res = query(PGRES_TUPLES_OK, cmd);
        REQUIRE(res.num_tuples() == 1);
        return res.get_value_as_string(0, 0);
    }

    int result_as_int(std::string const &cmd) const
    {
        return std::stoi(result_as_string(cmd));
    }

    double result_as_double(std::string const &cmd) const
    {
        return std::stod(result_as_string(cmd));
    }

    void assert_double(double expected, std::string const &cmd) const
    {
        REQUIRE(Approx(expected).epsilon(0.01) == result_as_double(cmd));
    }

    void assert_null(std::string const &cmd) const
    {
        pg_result_t const res = query(PGRES_TUPLES_OK, cmd);
        REQUIRE(res.num_tuples() == 1);
        REQUIRE(res.is_null(0, 0));
    }

    pg_result_t require_row(std::string const &cmd) const
    {
        pg_result_t res = query(PGRES_TUPLES_OK, cmd);
        REQUIRE(res.num_tuples() == 1);

        return res;
    }

    int get_count(char const *table_name, std::string const &where = "") const
    {
        auto const query = "SELECT count(*) FROM {} {} {}"_format(
            table_name, (where.empty() ? "" : "WHERE"), where);

        return result_as_int(query);
    }

    void require_has_table(char const *table_name) const
    {
        auto const where = "oid = '{}'::regclass"_format(table_name);

        REQUIRE(get_count("pg_catalog.pg_class", where) == 1);
    }
};

class tempdb_t
{
public:
    tempdb_t() noexcept
    {
        try {
            conn_t conn{"dbname=postgres"};

            m_db_name = "osm2pgsql-test-{}-{}"_format(getpid(), time(nullptr));
            conn.exec("DROP DATABASE IF EXISTS \"{}\""_format(m_db_name));
            conn.exec("CREATE DATABASE \"{}\" WITH ENCODING 'UTF8'"_format(
                m_db_name));

            conn_t local = connect();
            local.exec("CREATE EXTENSION postgis");
            local.exec("CREATE EXTENSION hstore");
        } catch (std::runtime_error const &e) {
            fmt::print(stderr,
                       "Test database cannot be created: {}\n"
                       "Did you mean to run 'pg_virtualenv ctest'?\n",
                       e.what());
            std::exit(1);
        }
    }

    tempdb_t(tempdb_t const &) = delete;
    tempdb_t &operator=(tempdb_t const &) = delete;

    tempdb_t(tempdb_t &&) = delete;
    tempdb_t &operator=(tempdb_t const &&) = delete;

    ~tempdb_t() noexcept
    {
        if (!m_db_name.empty()) {
            // Disable removal of the test database by setting the environment
            // variable OSM2PGSQL_KEEP_TEST_DB to anything. This can be useful
            // when debugging tests.
            char const *const keep_db = std::getenv("OSM2PGSQL_KEEP_TEST_DB");
            if (keep_db != nullptr) {
                return;
            }
            try {
                conn_t conn{"dbname=postgres"};
                conn.exec("DROP DATABASE IF EXISTS \"{}\""_format(m_db_name));
            } catch (...) {
                fprintf(stderr, "DROP DATABASE failed. Ignored.\n");
            }
        }
    }

    conn_t connect() const { return conn_t{conninfo()}; }

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

} // namespace pg
} // namespace testing

#endif // OSM2PGSQL_TESTS_COMMON_PG_HPP
