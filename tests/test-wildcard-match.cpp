/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2026 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <catch.hpp>

#include "wildcmp.hpp"

TEST_CASE("Wildcard matching", "[NoDB]")
{
    CHECK(wild_match("fhwieurwe", "fhwieurwe"));
    CHECK_FALSE(wild_match("fhwieurwe", "fhwieurw"));
    CHECK_FALSE(wild_match("fhwieurw", "fhwieurwe"));
    CHECK(wild_match("*", "foo"));
    CHECK(wild_match("**", "foo"));
    CHECK_FALSE(wild_match("r*", "foo"));
    CHECK(wild_match("r*", "roo"));
    CHECK(wild_match("*bar", "Hausbar"));
    CHECK_FALSE(wild_match("*bar", "Haustar"));
    CHECK(wild_match("*", ""));
    CHECK(wild_match("**", ""));
    CHECK(wild_match("kin*la", "kinla"));
    CHECK(wild_match("kin*la", "kinLLla"));
    CHECK(wild_match("kin*la", "kinlalalala"));
    CHECK(wild_match("kin**la", "kinlalalala"));
    CHECK_FALSE(wild_match("kin*la", "kinlaa"));
    CHECK_FALSE(wild_match("kin*la", "ki??laa"));
    CHECK(wild_match("1*2*3", "123"));
    CHECK(wild_match("1*2*3", "1xX23"));
    CHECK(wild_match("1*2*3", "12y23"));
    CHECK_FALSE(wild_match("1*2*3", "12"));
    CHECK(wild_match("bo??f", "boxxf"));
    CHECK_FALSE(wild_match("bo??f", "boxf"));
    CHECK(wild_match("?5?", "?5?"));
    CHECK(wild_match("?5?", "x5x"));
    CHECK_FALSE(wild_match("?abc", ""));
    CHECK_FALSE(wild_match("?", ""));
}
