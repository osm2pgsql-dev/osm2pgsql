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

#include <array>

using Coordinates = osmium::geom::Coordinates;

TEST_CASE("geom::distance", "[NoDB]")
{
    Coordinates const p1{10, 10};
    Coordinates const p2{20, 10};
    Coordinates const p3{13, 14};

    REQUIRE(geom::distance(p1, p1) == Approx(0.0));
    REQUIRE(geom::distance(p1, p2) == Approx(10.0));
    REQUIRE(geom::distance(p1, p3) == Approx(5.0));
}

TEST_CASE("geom::interpolate", "[NoDB]")
{
    Coordinates const p1{10, 10};
    Coordinates const p2{20, 10};

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
    ls1.add_point(Coordinates{17, 42});
    ls1.add_point(Coordinates{-3, 22});
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

TEST_CASE("geom::split_linestring w/o split", "[NoDB]")
{
    geom::linestring_t const line{Coordinates{0, 0}, Coordinates{1, 2},
                                  Coordinates{2, 2}};

    std::vector<geom::linestring_t> result;

    geom::split_linestring(line, 10.0, &result);

    REQUIRE(result.size() == 1);
    REQUIRE(result[0] == line);
}

TEST_CASE("geom::split_linestring with split 0.5", "[NoDB]")
{
    geom::linestring_t const line{Coordinates{0, 0}, Coordinates{1, 0}};

    std::array<geom::linestring_t, 2> const expected{
        geom::linestring_t{Coordinates{0, 0}, Coordinates{0.5, 0}},
        geom::linestring_t{Coordinates{0.5, 0}, Coordinates{1, 0}}};

    std::vector<geom::linestring_t> result;

    geom::split_linestring(line, 0.5, &result);

    REQUIRE(result.size() == 2);
    REQUIRE(result[0] == expected[0]);
    REQUIRE(result[1] == expected[1]);
}

TEST_CASE("geom::split_linestring with split 0.4", "[NoDB]")
{
    geom::linestring_t const line{Coordinates{0, 0}, Coordinates{1, 0}};

    std::array<geom::linestring_t, 3> const expected{
        geom::linestring_t{Coordinates{0, 0}, Coordinates{0.4, 0}},
        geom::linestring_t{Coordinates{0.4, 0}, Coordinates{0.8, 0}},
        geom::linestring_t{Coordinates{0.8, 0}, Coordinates{1, 0}}};

    std::vector<geom::linestring_t> result;

    geom::split_linestring(line, 0.4, &result);

    REQUIRE(result.size() == 3);
    REQUIRE(result[0] == expected[0]);
    REQUIRE(result[1] == expected[1]);
    REQUIRE(result[2] == expected[2]);
}

TEST_CASE("geom::split_linestring with split 1.0 at start", "[NoDB]")
{
    geom::linestring_t const line{Coordinates{0, 0}, Coordinates{2, 0},
                                  Coordinates{3, 0}, Coordinates{4, 0}};

    std::array<geom::linestring_t, 4> const expected{
        geom::linestring_t{Coordinates{0, 0}, Coordinates{1, 0}},
        geom::linestring_t{Coordinates{1, 0}, Coordinates{2, 0}},
        geom::linestring_t{Coordinates{2, 0}, Coordinates{3, 0}},
        geom::linestring_t{Coordinates{3, 0}, Coordinates{4, 0}}};

    std::vector<geom::linestring_t> result;

    geom::split_linestring(line, 1.0, &result);

    REQUIRE(result.size() == 4);
    REQUIRE(result[0] == expected[0]);
    REQUIRE(result[1] == expected[1]);
    REQUIRE(result[2] == expected[2]);
    REQUIRE(result[3] == expected[3]);
}

TEST_CASE("geom::split_linestring with split 1.0 in middle", "[NoDB]")
{
    geom::linestring_t const line{Coordinates{0, 0}, Coordinates{1, 0},
                                  Coordinates{3, 0}, Coordinates{4, 0}};

    std::array<geom::linestring_t, 4> const expected{
        geom::linestring_t{Coordinates{0, 0}, Coordinates{1, 0}},
        geom::linestring_t{Coordinates{1, 0}, Coordinates{2, 0}},
        geom::linestring_t{Coordinates{2, 0}, Coordinates{3, 0}},
        geom::linestring_t{Coordinates{3, 0}, Coordinates{4, 0}}};

    std::vector<geom::linestring_t> result;

    geom::split_linestring(line, 1.0, &result);

    REQUIRE(result.size() == 4);
    REQUIRE(result[0] == expected[0]);
    REQUIRE(result[1] == expected[1]);
    REQUIRE(result[2] == expected[2]);
    REQUIRE(result[3] == expected[3]);
}

TEST_CASE("geom::split_linestring with split 1.0 at end", "[NoDB]")
{
    geom::linestring_t const line{Coordinates{0, 0}, Coordinates{1, 0},
                                  Coordinates{2, 0}, Coordinates{4, 0}};

    std::array<geom::linestring_t, 4> const expected{
        geom::linestring_t{Coordinates{0, 0}, Coordinates{1, 0}},
        geom::linestring_t{Coordinates{1, 0}, Coordinates{2, 0}},
        geom::linestring_t{Coordinates{2, 0}, Coordinates{3, 0}},
        geom::linestring_t{Coordinates{3, 0}, Coordinates{4, 0}}};

    std::vector<geom::linestring_t> result;

    geom::split_linestring(line, 1.0, &result);

    REQUIRE(result.size() == 4);
    REQUIRE(result[0] == expected[0]);
    REQUIRE(result[1] == expected[1]);
    REQUIRE(result[2] == expected[2]);
    REQUIRE(result[3] == expected[3]);
}

