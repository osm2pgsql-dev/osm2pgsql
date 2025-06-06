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

} // anonymous namespace

TEST_CASE("hstore match only import")
{
    options_t options =
        testing::opt_t().slim().style("hstore-match-only.style");
    options.hstore_match_only = true;
    options.hstore_mode = hstore_column::norm;

    REQUIRE_NOTHROW(db.run_file(options, "hstore-match-only.osm"));

    auto conn = db.db().connect();

    // tables should not contain any tag columns
    REQUIRE(4 == conn.get_count("information_schema.columns",
                                "table_name='osm2pgsql_test_point'"));
    REQUIRE(5 == conn.get_count("information_schema.columns",
                                "table_name='osm2pgsql_test_polygon'"));
    REQUIRE(5 == conn.get_count("information_schema.columns",
                                "table_name='osm2pgsql_test_line'"));
    REQUIRE(5 == conn.get_count("information_schema.columns",
                                "table_name='osm2pgsql_test_roads'"));

    // the testfile contains 19 tagged ways and 7 tagged nodes
    // out of them 18 ways and 6 nodes are interesting as specified by hstore-match-only.style
    // as there is also one relation we should end up getting a database which contains:
    // 6 objects in osm2pgsql_test_point
    // 7 objects in osm2pgsql_test_polygon
    // 12 objects in osm2pgsql_test_line
    // 3 objects in osm2pgsql_test_roads

    REQUIRE(6 == conn.get_count("osm2pgsql_test_point"));
    REQUIRE(7 == conn.get_count("osm2pgsql_test_polygon"));
    REQUIRE(12 == conn.get_count("osm2pgsql_test_line"));
    REQUIRE(3 == conn.get_count("osm2pgsql_test_roads"));
}
