/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2020 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <catch.hpp>

#include "geom.hpp"

TEST_CASE("geom::distance", "[NoDB]")
{
    osmium::geom::Coordinates const p1{10, 10};
    osmium::geom::Coordinates const p2{20, 10};
    osmium::geom::Coordinates const p3{13, 14};

    REQUIRE(geom::distance(p1, p1) == Approx(0.0));
    REQUIRE(geom::distance(p1, p2) == Approx(10.0));
    REQUIRE(geom::distance(p1, p3) == Approx(5.0));
}

TEST_CASE("geom::interpolate", "[NoDB]")
{
    osmium::geom::Coordinates const p1{10, 10};
    osmium::geom::Coordinates const p2{20, 10};

    auto const i1 = geom::interpolate(p1, p1, 0.5);
    REQUIRE(i1.x == 10);
    REQUIRE(i1.y == 10);

    auto const i2 = geom::interpolate(p1, p2, 0.5);
    REQUIRE(i2.x == 15);
    REQUIRE(i2.y == 10);

    auto const i3 = geom::interpolate(p2, p1, 0.5);
    REQUIRE(i3.x == 15);
    REQUIRE(i3.y == 10);
}

TEST_CASE("geom::linestring_t", "[NoDB]")
{
    geom::linestring_t ls1;

    REQUIRE(ls1.empty());
    ls1.add_point(osmium::geom::Coordinates{17, 42});
    ls1.add_point(osmium::geom::Coordinates{-3, 22});
    REQUIRE(!ls1.empty());
    REQUIRE(ls1.size() == 2);

    auto it = ls1.cbegin();
    REQUIRE(it != ls1.cend());
    REQUIRE(it->x == 17);
    ++it;
    REQUIRE(it != ls1.cend());
    REQUIRE(it->y == 22);
    ++it;
    REQUIRE(it == ls1.cend());
}

