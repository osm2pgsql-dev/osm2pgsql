/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <catch.hpp>

#include "options.hpp"

/**
 * Tests that the conninfo strings are appropriately generated
 * This test is stricter than it needs to be, as it also cares about order,
 * but the current implementation always uses the same order, and attempting to
 * parse a conninfo string is complex.
 */
TEST_CASE("Connection info parsing with dbname", "[NoDB]")
{
    database_options_t db;
    CHECK(build_conninfo(db) ==
          "fallback_application_name='osm2pgsql' client_encoding='UTF8'");
    db.db = "foo";
    CHECK(build_conninfo(db) == "fallback_application_name='osm2pgsql' "
                                "client_encoding='UTF8' dbname='foo'");
}

TEST_CASE("Connection info parsing with user", "[NoDB]")
{
    database_options_t db;
    db.username = "bar";
    CHECK(build_conninfo(db) == "fallback_application_name='osm2pgsql' "
                                "client_encoding='UTF8' user='bar'");
}

TEST_CASE("Connection info parsing with password", "[NoDB]")
{
    database_options_t db;
    db.password = "bar";
    CHECK(build_conninfo(db) == "fallback_application_name='osm2pgsql' "
                                "client_encoding='UTF8' password='bar'");
}

TEST_CASE("Connection info parsing with host", "[NoDB]")
{
    database_options_t db;
    db.host = "bar";
    CHECK(build_conninfo(db) == "fallback_application_name='osm2pgsql' "
                                "client_encoding='UTF8' host='bar'");
}

TEST_CASE("Connection info parsing with port", "[NoDB]")
{
    database_options_t db;
    db.port = "bar";
    CHECK(build_conninfo(db) == "fallback_application_name='osm2pgsql' "
                                "client_encoding='UTF8' port='bar'");
}

TEST_CASE("Connection info parsing with complete info", "[NoDB]")
{
    database_options_t db;
    db.db = "foo";
    db.username = "bar";
    db.password = "baz";
    db.host = "bzz";
    db.port = "123";
    CHECK(build_conninfo(db) ==
          "fallback_application_name='osm2pgsql' client_encoding='UTF8' "
          "dbname='foo' "
          "user='bar' password='baz' host='bzz' port='123'");
}
