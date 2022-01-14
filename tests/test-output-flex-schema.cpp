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
#include "common-options.hpp"

static testing::db::import_t db;

static char const *const conf_file = "test_output_flex_schema.lua";
static char const *const data_file = "liechtenstein-2013-08-03.osm.pbf";

TEST_CASE("config with schema should work")
{
    options_t const options = testing::opt_t().slim().flex(conf_file);

    auto conn = db.db().connect();
    conn.exec("CREATE SCHEMA IF NOT EXISTS myschema;");

    REQUIRE_NOTHROW(db.run_file(options, data_file));

    REQUIRE(1 == conn.get_count("pg_namespace", "nspname = 'myschema'"));
    REQUIRE(1 == conn.get_count("pg_tables", "schemaname = 'myschema'"));

    REQUIRE(7103 == conn.get_count("myschema.osm2pgsql_test_line"));

    REQUIRE(1 ==
            conn.get_count("pg_proc",
                           "proname = 'osm2pgsql_test_line_osm2pgsql_valid'"));

    REQUIRE(1 == conn.get_count("pg_trigger"));
    REQUIRE(1 ==
            conn.get_count("pg_trigger",
                           "tgname = 'osm2pgsql_test_line_osm2pgsql_valid'"));
}
