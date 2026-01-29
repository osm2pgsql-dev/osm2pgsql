/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2026 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <catch.hpp>

#include "common-import.hpp"
#include "common-options.hpp"

namespace {

testing::db::import_t db;

char const *const CONF_FILE = "test_output_flex_validgeom.lua";
char const *const DATA_FILE = "test_output_pgsql_validgeom.osm";

} // anonymous namespace

TEST_CASE("no invalid geometries should end up in the database")
{
    options_t const options = testing::opt_t().flex(CONF_FILE);

    REQUIRE_NOTHROW(db.run_file(options, DATA_FILE));

    auto conn = db.db().connect();

    REQUIRE(12 == conn.get_count("osm2pgsql_test_polygon"));
    REQUIRE(0 ==
            conn.get_count("osm2pgsql_test_polygon", "NOT ST_IsValid(geom)"));
    REQUIRE(0 == conn.get_count("osm2pgsql_test_polygon", "ST_IsEmpty(geom)"));
}
