/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <catch.hpp>

#include "geom-functions.hpp"
#include "geom.hpp"

TEST_CASE("polygon geometry without inner", "[NoDB]")
{
    geom::geometry_t const geom{
        geom::polygon_t{geom::ring_t{{0, 0}, {0, 1}, {1, 1}, {1, 0}, {0, 0}}}};

    REQUIRE(num_geometries(geom) == 1);
    REQUIRE(area(geom) == Approx(1.0));
    REQUIRE(geometry_type(geom) == "POLYGON");
    REQUIRE(centroid(geom) == geom::geometry_t{geom::point_t{0.5, 0.5}});
}

TEST_CASE("polygon geometry without inner (reverse)", "[NoDB]")
{
    geom::geometry_t const geom{
        geom::polygon_t{geom::ring_t{{0, 0}, {1, 0}, {1, 1}, {0, 1}, {0, 0}}}};

    REQUIRE(num_geometries(geom) == 1);
    REQUIRE(area(geom) == Approx(1.0));
    REQUIRE(geometry_type(geom) == "POLYGON");
    REQUIRE(centroid(geom) == geom::geometry_t{geom::point_t{0.5, 0.5}});
}

TEST_CASE("polygon geometry with inner", "[NoDB]")
{
    geom::geometry_t const geom{
        geom::polygon_t{geom::ring_t{{0, 0}, {0, 1}, {1, 1}, {1, 0}, {0, 0}}}};

    REQUIRE(num_geometries(geom) == 1);
    REQUIRE(area(geom) == Approx(1.0));
    REQUIRE(geometry_type(geom) == "POLYGON");
    REQUIRE(centroid(geom) == geom::geometry_t{geom::point_t{0.5, 0.5}});
}

TEST_CASE("geom::polygon_t", "[NoDB]")
{
    geom::polygon_t polygon;

    REQUIRE(polygon.outer().empty());
    polygon.outer() = geom::ring_t{{0, 0}, {0, 3}, {3, 3}, {3, 0}, {0, 0}};
    polygon.inners().emplace_back(
        geom::ring_t{{1, 1}, {2, 1}, {2, 2}, {1, 2}, {1, 1}});

    REQUIRE(polygon.num_geometries() == 1);
    REQUIRE(polygon.inners().size() == 1);

    geom::geometry_t geom{std::move(polygon)};
    REQUIRE(num_geometries(geom) == 1);
    REQUIRE(area(geom) == Approx(8.0));
    REQUIRE(geometry_type(geom) == "POLYGON");
    REQUIRE(centroid(geom) == geom::geometry_t{geom::point_t{1.5, 1.5}});
}
