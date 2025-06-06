/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2025 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <catch.hpp>

#include "common-import.hpp"
#include "common-options.hpp"

namespace {

testing::db::import_t db;

char const *const conf_file = "test_output_flex.lua";

} // anonymous namespace

TEST_CASE("simple import with tablespaces for middle")
{
    {
        auto conn = db.db().connect();
        REQUIRE(1 == conn.get_count("pg_catalog.pg_tablespace",
                                    "spcname = 'tablespacetest'"));
    }

    options_t options = testing::opt_t().slim().flex(conf_file);
    options.tblsslim_index = "tablespacetest";
    options.tblsslim_data = "tablespacetest";

    REQUIRE_NOTHROW(db.run_file(options, "liechtenstein-2013-08-03.osm.pbf"));

    auto conn = db.db().connect();

    conn.require_has_table("osm2pgsql_test_point");
    conn.require_has_table("osm2pgsql_test_line");
    conn.require_has_table("osm2pgsql_test_polygon");

    REQUIRE(1362 == conn.get_count("osm2pgsql_test_point"));
    REQUIRE(2932 == conn.get_count("osm2pgsql_test_line"));
    REQUIRE(4136 == conn.get_count("osm2pgsql_test_polygon"));
    REQUIRE(35 == conn.get_count("osm2pgsql_test_route"));
}
