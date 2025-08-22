#ifndef OSM2PGSQL_TESTS_COMMON_PG_HPP
#define OSM2PGSQL_TESTS_COMMON_PG_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2025 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <string_view>

#include "format.hpp"
#include "options.hpp"
#include "pgsql.hpp"
#include "pgsql-capabilities.hpp"
#include <catch.hpp>

#ifdef _MSC_VER
#include <process.h>
#include <windows.h>
#define getpid _getpid
#else
#include <sys/types.h>
#include <unistd.h>
#endif

/// Helper classes for postgres connections
namespace testing::pg {

class conn_t : public pg_conn_t
{
public:
    explicit conn_t(connection_params_t const &connection_params)
    : pg_conn_t(connection_params, "test")
    {
    }

    std::string result_as_string(std::string const &cmd) const
    {
        pg_result_t const res = exec(cmd);
        REQUIRE(res.num_tuples() == 1);
        return std::string{res.get(0, 0)};
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
        pg_result_t const res = exec(cmd);
        REQUIRE(res.num_tuples() == 1);
        REQUIRE(res.is_null(0, 0));
    }

    pg_result_t require_row(std::string const &cmd) const
    {
        pg_result_t res = exec(cmd);
        REQUIRE(res.num_tuples() == 1);

        return res;
    }

    int get_count(std::string_view table_name,
                  std::string_view where = "") const
    {
        auto const query =
            fmt::format("SELECT count(*) FROM {} {} {}", table_name,
                        (where.empty() ? "" : "WHERE"), where);

        return result_as_int(query);
    }

    void require_has_table(std::string_view table_name) const
    {
        auto const where = fmt::format("oid = '{}'::regclass", table_name);

        REQUIRE(get_count("pg_catalog.pg_class", where) == 1);
    }
};

class tempdb_t
{
public:
    tempdb_t() noexcept
    {
        try {
            connection_params_t connection_params;
            connection_params.set("dbname", "postgres");
            conn_t const conn{connection_params};

            m_db_name =
                fmt::format("osm2pgsql-test-{}-{}", getpid(), time(nullptr));
            conn.exec(R"(DROP DATABASE IF EXISTS "{}")", m_db_name);
            conn.exec(R"(CREATE DATABASE "{}" WITH ENCODING 'UTF8')",
                      m_db_name);

            conn_t const local = connect();
            local.exec("CREATE EXTENSION postgis");
            local.exec("CREATE EXTENSION hstore");
            init_database_capabilities(local);
        } catch (std::runtime_error const &e) {
            fmt::print(stderr,
                       "Test database cannot be created: {}\n"
                       "Did you mean to run 'pg_virtualenv ctest'?\n",
                       e.what());
            std::exit(1); // NOLINT(concurrency-mt-unsafe)
        }
    }

    tempdb_t(tempdb_t const &) = delete;
    tempdb_t &operator=(tempdb_t const &) = delete;

    tempdb_t(tempdb_t &&) = delete;
    tempdb_t &operator=(tempdb_t const &&) = delete;

    // We want to terminate the program if there is an exception thrown inside
    // the destructor.
    // NOLINTNEXTLINE(bugprone-exception-escape)
    ~tempdb_t() noexcept
    {
        if (m_db_name.empty()) {
            return;
        }

        // Disable removal of the test database by setting the environment
        // variable OSM2PGSQL_KEEP_TEST_DB to anything. This can be useful
        // when debugging tests.
        // NOLINTNEXTLINE(concurrency-mt-unsafe)
        char const *const keep_db = std::getenv("OSM2PGSQL_KEEP_TEST_DB");
        if (keep_db != nullptr) {
            return;
        }
        try {
            connection_params_t connection_params;
            connection_params.set("dbname", "postgres");
            conn_t const conn{connection_params};
            conn.exec(R"(DROP DATABASE IF EXISTS "{}")", m_db_name);
        } catch (...) {
            fmt::print(stderr, "DROP DATABASE failed. Ignored.\n");
        }
    }

    conn_t connect() const { return conn_t{connection_params()}; }

    connection_params_t connection_params() const {
        connection_params_t params;
        params.set("dbname", m_db_name);
        return params;
    }

private:
    std::string m_db_name;
};

} // namespace testing::pg

#endif // OSM2PGSQL_TESTS_COMMON_PG_HPP
