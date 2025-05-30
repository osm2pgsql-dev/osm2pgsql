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

#include <array>

namespace {

testing::db::import_t db;

} // anonymous namespace

TEST_CASE("compute Z order")
{
    REQUIRE_NOTHROW(
        db.run_file(testing::opt_t().slim(), "test_output_pgsql_z_order.osm"));

    auto conn = db.db().connect();

    std::array<char const *, 5> const expected = {
        "motorway", "trunk", "primary", "secondary", "tertiary"};

    for (unsigned i = 0; i < 5; ++i) {
        auto const sql = fmt::format("SELECT highway"
                                     " FROM osm2pgsql_test_line"
                                     " WHERE layer IS NULL"
                                     " ORDER BY z_order DESC"
                                     " LIMIT 1 OFFSET {}",
                                     i);
        REQUIRE(expected.at(i) == conn.result_as_string(sql));
    }

    REQUIRE("residential" ==
            conn.result_as_string("SELECT highway FROM osm2pgsql_test_line "
                                  "ORDER BY z_order DESC LIMIT 1 OFFSET 0"));
}
