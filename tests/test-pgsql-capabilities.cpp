/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <catch.hpp>

#include "common-import.hpp"

#include "pgsql-capabilities.hpp"

static testing::db::import_t const db;

TEST_CASE("has_extension() should work")
{
    auto const conn = db.db().connect();
    REQUIRE(has_extension(conn, "postgis"));
    REQUIRE_FALSE(has_schema(conn, "xzxzxzxz"));
}

TEST_CASE("has_schema() should work")
{
    auto const conn = db.db().connect();
    REQUIRE(has_schema(conn, "public"));
    REQUIRE_FALSE(has_schema(conn, "xzxzxzxz"));
    REQUIRE_FALSE(has_schema(conn, "pg_toast"));
}

TEST_CASE("has_tablespace() should work")
{
    auto const conn = db.db().connect();
    REQUIRE(has_tablespace(conn, "pg_default"));
    REQUIRE_FALSE(has_tablespace(conn, "xzxzxzxz"));
    REQUIRE_FALSE(has_tablespace(conn, "pg_global"));
}
