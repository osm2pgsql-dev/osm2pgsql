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
    init_database_capabilities(db.db().connect());
    REQUIRE(has_extension("postgis"));
    REQUIRE_FALSE(has_schema("xzxzxzxz"));
}

TEST_CASE("has_schema() should work")
{
    init_database_capabilities(db.db().connect());
    REQUIRE(has_schema("public"));
    REQUIRE_FALSE(has_schema("xzxzxzxz"));
    REQUIRE_FALSE(has_schema("pg_toast"));
}

TEST_CASE("has_tablespace() should work")
{
    init_database_capabilities(db.db().connect());
    REQUIRE(has_tablespace("pg_default"));
    REQUIRE_FALSE(has_tablespace("xzxzxzxz"));
    REQUIRE_FALSE(has_tablespace("pg_global"));
}

TEST_CASE("has_index_method() should work")
{
    init_database_capabilities(db.db().connect());
    REQUIRE(has_index_method("btree"));
    REQUIRE_FALSE(has_index_method("xzxzxzxz"));
}

TEST_CASE("PostgreSQL version")
{
    init_database_capabilities(db.db().connect());
    auto const version = get_database_version();
    REQUIRE(version >= 9);
}

TEST_CASE("PostGIS version")
{
    init_database_capabilities(db.db().connect());
    auto const postgis_version = get_postgis_version();
    REQUIRE(postgis_version.major >= 2);
}
