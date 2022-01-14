/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <catch.hpp>

#include "node-locations.hpp"

TEST_CASE("node locations basics", "[NoDB]")
{
    node_locations_t nl;
    REQUIRE(nl.size() == 0);

    REQUIRE(nl.set(3, {1.2, 3.4}));
    REQUIRE(nl.set(5, {5.6, 7.8}));

    REQUIRE(nl.size() == 2);

    REQUIRE(nl.get(1) == osmium::Location{});
    REQUIRE(nl.get(4) == osmium::Location{});
    REQUIRE(nl.get(6) == osmium::Location{});
    REQUIRE(nl.get(100) == osmium::Location{});

    REQUIRE(nl.get(3) == osmium::Location{1.2, 3.4});
    REQUIRE(nl.get(5) == osmium::Location{5.6, 7.8});

    nl.clear();
    REQUIRE(nl.size() == 0);
}

TEST_CASE("node locations in more than one block", "[NoDB]")
{
    node_locations_t nl;

    osmid_t max_id = 0;

    SECTION("max_id 0") {
        max_id = 0;
    }

    SECTION("max_id 31") {
        max_id = 31;
    }

    SECTION("max_id 32") {
        max_id = 32;
    }

    SECTION("max_id 33") {
        max_id = 33;
    }

    SECTION("max_id 64") {
        max_id = 64;
    }

    SECTION("max_id 80") {
        max_id = 80;
    }

    for (osmid_t id = 1; id <= max_id; ++id) {
        nl.set(id, {id + 0.1, id + 0.2});
    }

    REQUIRE(static_cast<osmid_t>(nl.size()) == max_id);

    for (osmid_t id = 1; id <= max_id; ++id) {
        auto const location = nl.get(id);
        REQUIRE(location.lon() == id + 0.1);
        REQUIRE(location.lat() == id + 0.2);
    }
}

TEST_CASE("huge ids should work", "[NoDB]")
{
    node_locations_t nl;

    REQUIRE(nl.set(1ULL, {1.0, 9.9}));
    REQUIRE(nl.set(1ULL << 16U, {1.1, 9.8}));
    REQUIRE(nl.set(1ULL << 32U, {1.2, 9.7}));
    REQUIRE(nl.set(1ULL << 48U, {1.3, 9.6}));

    REQUIRE(nl.size() == 4);

    REQUIRE(nl.get(1ULL) == osmium::Location{1.0, 9.9});
    REQUIRE(nl.get(1ULL << 16U) == osmium::Location{1.1, 9.8});
    REQUIRE(nl.get(1ULL << 32U) == osmium::Location{1.2, 9.7});
    REQUIRE(nl.get(1ULL << 48U) == osmium::Location{1.3, 9.6});

    REQUIRE(nl.get(2ULL) == osmium::Location{});
    REQUIRE(nl.get(1ULL << 40U) == osmium::Location{});
    REQUIRE(nl.get((1ULL << 48U) + 1U) == osmium::Location{});
    REQUIRE(nl.get((1ULL << 48U) - 1U) == osmium::Location{});
}

TEST_CASE("full node locations store", "[NoDB]")
{
    node_locations_t nl{30};
    REQUIRE(nl.size() == 0);

    REQUIRE(nl.set(3, {1.2, 3.4}));
    REQUIRE_FALSE(nl.set(5, {5.6, 7.8}));

    REQUIRE(nl.size() == 1);
}

