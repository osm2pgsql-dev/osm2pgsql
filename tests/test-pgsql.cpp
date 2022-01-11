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

TEST_CASE("PostGIS version")
{
    auto conn = db.db().connect();
    auto const postgis_version = get_postgis_version(conn);
    REQUIRE(postgis_version.major >= 2);
}
