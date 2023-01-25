/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <catch.hpp>

#include "common-buffer.hpp"

#include "geom-from-osm.hpp"
#include "geom-functions.hpp"
#include "geom-output.hpp"
#include "geom-pole-of-inaccessibility.hpp"
#include "geom.hpp"

TEST_CASE("null geometry returns null geom", "[NoDB]")
{
    geom::geometry_t const geom{};

    REQUIRE(centroid(geom).is_null());
    REQUIRE(pole_of_inaccessibility(geom, 0.01).is_null());
}

TEST_CASE("polygon geometry without inner", "[NoDB]")
{
    geom::geometry_t const geom{
        geom::polygon_t{geom::ring_t{{0, 0}, {0, 1}, {1, 1}, {1, 0}, {0, 0}}}};

    REQUIRE(centroid(geom) == geom::geometry_t{geom::point_t{0.5, 0.5}});
    REQUIRE(pole_of_inaccessibility(geom, 0.01) ==
            geom::geometry_t{geom::point_t{0.5, 0.5}});
}

TEST_CASE("polygon geometry without inner (reverse)", "[NoDB]")
{
    geom::geometry_t const geom{
        geom::polygon_t{geom::ring_t{{0, 0}, {1, 0}, {1, 1}, {0, 1}, {0, 0}}}};

    REQUIRE(centroid(geom) == geom::geometry_t{geom::point_t{0.5, 0.5}});
    REQUIRE(pole_of_inaccessibility(geom, 0.01) ==
            geom::geometry_t{geom::point_t{0.5, 0.5}});
}

TEST_CASE("geom::polygon_t", "[NoDB]")
{
    geom::polygon_t polygon;

    REQUIRE(polygon.outer().empty());
    polygon.outer() = geom::ring_t{{0, 0}, {0, 3}, {4, 3}, {4, 0}, {0, 0}};
    polygon.inners().emplace_back(
        geom::ring_t{{1, 1}, {2, 1}, {2, 2}, {1, 2}, {1, 1}});

    geom::geometry_t const geom{std::move(polygon)};
    auto const middle =
        pole_of_inaccessibility(geom, 0.00001).get<geom::point_t>();
    REQUIRE(middle.x() == Approx(3.0).epsilon(0.001));
    REQUIRE((middle.y() >= 1.0 && middle.y() <= 2.0));
}

TEST_CASE("pole_of_inaccessibility with stretch factor", "[NoDB]")
{
    geom::geometry_t const geom{geom::polygon_t{
        geom::ring_t{{0, 0}, {0, 3}, {1, 3}, {1, 1}, {2, 1}, {2, 0}, {0, 0}}}};

    REQUIRE(pole_of_inaccessibility(geom, 0.01, 2) ==
            geom::geometry_t{geom::point_t{1.0, 0.5}});
}
