/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2025 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <catch.hpp>

#include "locator.hpp"

TEST_CASE("Create empty locator", "[NoDB]")
{
    locator_t locator;
    REQUIRE(locator.name().empty());
    REQUIRE(locator.empty());
    REQUIRE(locator.size() == 0); // NOLINT(readability-container-size-empty)

    locator.set_name("foo");
    REQUIRE(locator.name() == "foo");
}

TEST_CASE("Create locator with single box and check it", "[NoDB]")
{
    locator_t locator;
    locator.set_name("box");
    locator.add_region("in", geom::box_t{0, 0, 10, 10});

    REQUIRE_FALSE(locator.empty());
    REQUIRE(locator.size() == 1);

    locator.build_index();

    geom::geometry_t const p1{geom::point_t{0.5, 0.5}}; // in box
    geom::geometry_t const p2{geom::point_t{20, 20}}; // outside box
    geom::geometry_t const p3{geom::point_t{0, 0}}; // on boundary

    REQUIRE(locator.first_intersecting(p1) == "in");
    REQUIRE(locator.first_intersecting(p2).empty());
    REQUIRE(locator.first_intersecting(p3) == "in");

    auto const a1 = locator.all_intersecting(p1);
    auto const a2 = locator.all_intersecting(p2);
    auto const a3 = locator.all_intersecting(p3);

    REQUIRE(a1.size() == 1);
    REQUIRE(a1.count("in") == 1);
    REQUIRE(a2.empty());
    REQUIRE(a3.size() == 1);
    REQUIRE(a3.count("in") == 1);
}

TEST_CASE("Create locator with multiple boxes and check it", "[NoDB]")
{
    locator_t locator;
    locator.set_name("box");
    locator.add_region("b1", geom::box_t{0, 0, 20, 20});
    locator.add_region("b2", geom::box_t{10, 10, 30, 30});

    REQUIRE_FALSE(locator.empty());
    REQUIRE(locator.size() == 2);

    geom::geometry_t const p1{geom::point_t{1, 1}}; // in 1
    geom::geometry_t const p2{geom::point_t{11, 21}}; // in 2
    geom::geometry_t const p3{geom::point_t{11, 11}}; // in 1 and 2
    geom::geometry_t const p4{geom::point_t{1, 40}}; // outside

    REQUIRE(locator.first_intersecting(p1) == "b1");
    REQUIRE(locator.first_intersecting(p2) == "b2");
    REQUIRE(((locator.first_intersecting(p3) == "b1") ||
             (locator.first_intersecting(p3) == "b2")));
    REQUIRE(locator.first_intersecting(p4).empty());

    auto const a1 = locator.all_intersecting(p1);
    auto const a2 = locator.all_intersecting(p2);
    auto const a3 = locator.all_intersecting(p3);
    auto const a4 = locator.all_intersecting(p4);

    REQUIRE(a1.size() == 1);
    REQUIRE(a2.size() == 1);
    REQUIRE(a3.size() == 2);
    REQUIRE(a4.empty());

    REQUIRE(a1.count("b1") == 1);
    REQUIRE(a2.count("b2") == 1);
    REQUIRE(a3.count("b1") == 1);
    REQUIRE(a3.count("b2") == 1);
}

TEST_CASE("Locator with polygon regions", "[NoDB]")
{
    locator_t locator;
    locator.set_name("box");
    locator.add_region("b1", geom::box_t{0, 0, 5, 5});

    geom::point_t const c1{0, 0};
    geom::point_t const c2{0, 5};
    geom::point_t const c3{5, 0};
    geom::point_t const c4{5, 5};

    geom::polygon_t polygon1{geom::ring_t{c1, c2, c3, c1}};
    geom::polygon_t polygon2{geom::ring_t{c4, c2, c3, c4}};

    locator.add_region("p1", geom::geometry_t{std::move(polygon1)});
    locator.add_region("p2", geom::geometry_t{std::move(polygon2)});

    REQUIRE_FALSE(locator.empty());
    REQUIRE(locator.size() == 3);

    geom::geometry_t const p1{geom::point_t{1, 1}}; // in b1, p1
    geom::geometry_t const p2{geom::point_t{4, 4}}; // in b1, p2
    geom::geometry_t const p3{geom::point_t{1, 10}}; // outside

    REQUIRE(((locator.first_intersecting(p1) == "b1") ||
             (locator.first_intersecting(p1) == "p1")));

    REQUIRE(((locator.first_intersecting(p2) == "b1") ||
             (locator.first_intersecting(p2) == "p2")));

    REQUIRE(locator.first_intersecting(p3).empty());

    auto const a1 = locator.all_intersecting(p1);
    auto const a2 = locator.all_intersecting(p2);
    auto const a3 = locator.all_intersecting(p3);

    REQUIRE(a1.size() == 2);
    REQUIRE(a2.size() == 2);
    REQUIRE(a3.empty());

    REQUIRE(a1.count("b1") == 1);
    REQUIRE(a1.count("p1") == 1);
    REQUIRE(a2.count("b1") == 1);
    REQUIRE(a2.count("p2") == 1);
}
