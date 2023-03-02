/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <catch.hpp>

#include "common-import.hpp"
#include "pgsql.hpp"

static testing::db::import_t const db;

TEST_CASE("Tablespace clause with no tablespace")
{
    REQUIRE(tablespace_clause("").empty());
}

TEST_CASE("Tablespace clause with tablespace")
{
    REQUIRE(tablespace_clause("foo") == R"( TABLESPACE "foo")");
}

TEST_CASE("Table name without schema")
{
    REQUIRE(qualified_name("", "foo") == R"("foo")");
}

TEST_CASE("Table name with schema")
{
    REQUIRE(qualified_name("osm", "foo") == R"("osm"."foo")");
}

TEST_CASE("query with SELECT should work")
{
    auto const conn = db.db().connect();
    auto const result = conn.exec("SELECT 42");
    REQUIRE(result.status() == PGRES_TUPLES_OK);
    REQUIRE(result.num_fields() == 1);
    REQUIRE(result.num_tuples() == 1);
    REQUIRE(result.get(0, 0) == "42");
}

TEST_CASE("query with invalid SQL should fail")
{
    auto const conn = db.db().connect();
    REQUIRE_THROWS(conn.exec("NOT-VALID-SQL"));
}

TEST_CASE("exec with invalid SQL should fail")
{
    auto const conn = db.db().connect();
    REQUIRE_THROWS(conn.exec("XYZ"));
}

TEST_CASE("exec_prepared with single string parameters should work")
{
    auto const conn = db.db().connect();
    conn.exec("PREPARE test(int) AS SELECT $1");

    auto const result = conn.exec_prepared("test", "17");
    REQUIRE(result.status() == PGRES_TUPLES_OK);
    REQUIRE(result.num_fields() == 1);
    REQUIRE(result.num_tuples() == 1);
    REQUIRE(result.get(0, 0) == "17");
}

TEST_CASE("exec_prepared with string parameters should work")
{
    auto const conn = db.db().connect();
    conn.exec("PREPARE test(int, int, int, int, int)"
              " AS SELECT $1 + $2 + $3 + $4 + $5");

    std::string a{"4"}; // NOLINT(misc-const-correctness)
    std::string const b{"5"};
    auto const result =
        conn.exec_prepared("test", "1", "2", std::string{"3"}, a, b);
    REQUIRE(result.status() == PGRES_TUPLES_OK);
    REQUIRE(result.num_fields() == 1);
    REQUIRE(result.num_tuples() == 1);
    REQUIRE(result.get(0, 0) == "15");
}

TEST_CASE("exec_prepared with non-string parameters should work")
{
    auto const conn = db.db().connect();
    conn.exec("PREPARE test(int, int, int) AS SELECT $1 + $2 + $3");

    auto const result = conn.exec_prepared("test", 1, 2.0, 3ULL);
    REQUIRE(result.status() == PGRES_TUPLES_OK);
    REQUIRE(result.num_fields() == 1);
    REQUIRE(result.num_tuples() == 1);
    REQUIRE(result.get(0, 0) == "6");
}

TEST_CASE("exec_prepared with binary parameter should work")
{
    auto const conn = db.db().connect();
    conn.exec("PREPARE test(bytea) AS SELECT length($1)");

    binary_param const p{"foo \x01 bar"};
    auto const result = conn.exec_prepared("test", p);
    REQUIRE(result.status() == PGRES_TUPLES_OK);
    REQUIRE(result.num_fields() == 1);
    REQUIRE(result.num_tuples() == 1);
    REQUIRE(result.get(0, 0) == "9");
}

TEST_CASE("exec_prepared with mixed parameter types should work")
{
    auto const conn = db.db().connect();
    conn.exec("PREPARE test(text, bytea, int) AS"
              " SELECT length($1) + length($2) + $3");

    std::string const p1{"foo bar"};
    binary_param const p2{"foo \x01 bar"};
    int const p3 = 17;
    auto const result = conn.exec_prepared("test", p1, p2, p3);
    REQUIRE(result.status() == PGRES_TUPLES_OK);
    REQUIRE(result.num_fields() == 1);
    REQUIRE(result.num_tuples() == 1);
    REQUIRE(result.get(0, 0) == "33"); // 7 + 9 + 17
}

TEST_CASE("create table and insert something")
{
    auto const conn = db.db().connect();
    conn.exec("CREATE TABLE foo (x int)");
    auto const result = conn.exec("INSERT INTO foo (x) VALUES (1), (2)");
    REQUIRE(result.status() == PGRES_COMMAND_OK);
    REQUIRE(result.num_fields() == 0);
    REQUIRE(result.num_tuples() == 0);
    REQUIRE(result.affected_rows() == 2);
}
