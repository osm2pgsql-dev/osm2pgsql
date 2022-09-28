/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <catch.hpp>

#include "geom-output.hpp"

#include <sstream>

TEST_CASE("nullgeom_t output", "[NoDB]")
{
    geom::nullgeom_t g;
    std::stringstream ss1;
    ss1 << g;
    CHECK(ss1.str() == "NULL");

    geom::geometry_t geom;
    std::stringstream ss2;
    ss2 << geom;
    CHECK(ss2.str() == "NULL(NULL)");
}

TEST_CASE("point_t output", "[NoDB]")
{
    geom::point_t g{1, 2};
    std::stringstream ss1;
    ss1 << g;
    CHECK(ss1.str() == "1 2");

    geom::geometry_t geom{std::move(g)};
    std::stringstream ss2;
    ss2 << geom;
    CHECK(ss2.str() == "POINT(1 2)");
}

TEST_CASE("linestring_t output", "[NoDB]")
{
    geom::linestring_t g{{1, 2}, {2, 2}};
    std::stringstream ss1;
    ss1 << g;
    CHECK(ss1.str() == "1 2,2 2");

    geom::geometry_t geom{std::move(g)};
    std::stringstream ss2;
    ss2 << geom;
    CHECK(ss2.str() == "LINESTRING(1 2,2 2)");
}

TEST_CASE("polygon_t with no inner rings output", "[NoDB]")
{
    geom::polygon_t g{geom::ring_t{{0, 0}, {1, 0}, {1, 1}, {0, 1}, {0, 0}}};
    std::stringstream ss1;
    ss1 << g;
    CHECK(ss1.str() == "(0 0,1 0,1 1,0 1,0 0)");

    geom::geometry_t geom{std::move(g)};
    std::stringstream ss2;
    ss2 << geom;
    CHECK(ss2.str() == "POLYGON((0 0,1 0,1 1,0 1,0 0))");
}

TEST_CASE("polygon_t with inner ring output", "[NoDB]")
{
    geom::polygon_t g{geom::ring_t{{0, 0}, {3, 0}, {3, 3}, {0, 3}, {0, 0}}};
    g.add_inner_ring(geom::ring_t{{1, 1}, {1, 2}, {2, 2}, {2, 1}, {1, 1}});
    std::stringstream ss1;
    ss1 << g;
    CHECK(ss1.str() == "(0 0,3 0,3 3,0 3,0 0),(1 1,1 2,2 2,2 1,1 1)");

    geom::geometry_t geom{std::move(g)};
    std::stringstream ss2;
    ss2 << geom;
    CHECK(ss2.str() == "POLYGON((0 0,3 0,3 3,0 3,0 0),(1 1,1 2,2 2,2 1,1 1))");
}

TEST_CASE("multipoint_t output", "[NoDB]")
{
    geom::multipoint_t g;
    g.add_geometry({1, 2});
    g.add_geometry({4, 3});
    std::stringstream ss1;
    ss1 << g;
    CHECK(ss1.str() == "(1 2),(4 3)");

    geom::geometry_t geom{std::move(g)};
    std::stringstream ss2;
    ss2 << geom;
    CHECK(ss2.str() == "MULTIPOINT((1 2),(4 3))");
}

TEST_CASE("multilinestring_t output", "[NoDB]")
{
    geom::multilinestring_t g;
    g.add_geometry({{1, 2}, {2, 2}});
    g.add_geometry({{4, 3}, {1, 1}});
    std::stringstream ss1;
    ss1 << g;
    CHECK(ss1.str() == "(1 2,2 2),(4 3,1 1)");

    geom::geometry_t geom{std::move(g)};
    std::stringstream ss2;
    ss2 << geom;
    CHECK(ss2.str() == "MULTILINESTRING((1 2,2 2),(4 3,1 1))");
}

TEST_CASE("multipolygon_t output", "[NoDB]")
{
    geom::multipolygon_t g;
    g.add_geometry(geom::polygon_t{geom::ring_t{{0, 0}, {0, 1}, {1, 1}}});
    g.add_geometry(geom::polygon_t{geom::ring_t{{2, 2}, {2, 3}, {3, 2}}});
    std::stringstream ss1;
    ss1 << g;
    CHECK(ss1.str() == "((0 0,0 1,1 1)),((2 2,2 3,3 2))");

    geom::geometry_t geom{std::move(g)};
    std::stringstream ss2;
    ss2 << geom;
    CHECK(ss2.str() == "MULTIPOLYGON(((0 0,0 1,1 1)),((2 2,2 3,3 2)))");
}

TEST_CASE("collection_t output", "[NoDB]")
{
    geom::collection_t g;
    g.add_geometry(geom::geometry_t{
        geom::polygon_t{geom::ring_t{{0, 0}, {0, 1}, {1, 1}}}});
    g.add_geometry(geom::geometry_t{geom::point_t{2, 3}});
    std::stringstream ss1;
    ss1 << g;
    CHECK(ss1.str() == "POLYGON((0 0,0 1,1 1)),POINT(2 3)");

    geom::geometry_t geom{std::move(g)};
    std::stringstream ss2;
    ss2 << geom;
    CHECK(ss2.str() == "GEOMETRYCOLLECTION(POLYGON((0 0,0 1,1 1)),POINT(2 3))");
}
