/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2025 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <catch.hpp>

#include "common-buffer.hpp"

#include "geom-from-osm.hpp"
#include "geom-functions.hpp"
#include "geom-output.hpp"
#include "geom.hpp"

TEST_CASE("geom::point_t", "[NoDB]")
{
    geom::point_t p;

    REQUIRE(p.x() == Approx(0.0));
    REQUIRE(p.y() == Approx(0.0));

    p.set_x(1.2);
    p.set_y(3.4);

    REQUIRE(p.x() == Approx(1.2));
    REQUIRE(p.y() == Approx(3.4));

    REQUIRE(p.num_geometries() == 1);
}

TEST_CASE("geom::point_t from location", "[NoDB]")
{
    osmium::Location const location{3.141, 2.718};
    geom::point_t const p{location};

    REQUIRE(p.x() == Approx(3.141));
    REQUIRE(p.y() == Approx(2.718));
    REQUIRE(p == geom::point_t{3.141, 2.718});
}

TEST_CASE("geom::point_t from location with create_point", "[NoDB]")
{
    osmium::Location const location{1.1, 2.2};

    geom::geometry_t geom;
    geom::create_point(&geom, location);
    REQUIRE(geom.is_point());

    auto const &p = geom.get<geom::point_t>();
    REQUIRE(p.x() == Approx(1.1));
    REQUIRE(p.y() == Approx(2.2));
    REQUIRE(p == geom::point_t{1.1, 2.2});
}

TEST_CASE("create_point from OSM data", "[NoDB]")
{
    test_buffer_t buffer;
    buffer.add_node("n10 x1.1 y2.2");

    auto const geom = geom::create_point(buffer.buffer().get<osmium::Node>(0));

    REQUIRE(geom.is_point());
    REQUIRE(geometry_type(geom) == "POINT");
    REQUIRE(dimension(geom) == 0);
    REQUIRE(num_geometries(geom) == 1);
    REQUIRE(area(geom) == Approx(0.0));
    REQUIRE(length(geom) == Approx(0.0));
    REQUIRE(centroid(geom) == geom::geometry_t{geom::point_t{1.1, 2.2}});
    REQUIRE(geometry_n(geom, 1) == geom);
    REQUIRE(reverse(geom) == geom);
    REQUIRE(geom.get<geom::point_t>() == geom::point_t{1.1, 2.2});
}

TEST_CASE("point order", "[NoDB]")
{
    geom::point_t const p{10, 10};
    REQUIRE_FALSE(p < p);
    REQUIRE_FALSE(p > p);

    std::vector<geom::point_t> points = {
        {10, 10}, {20, 10}, {13, 14}, {13, 10}
    };

    std::sort(points.begin(), points.end());

    REQUIRE(points[0] == geom::point_t(10, 10));
    REQUIRE(points[1] == geom::point_t(13, 10));
    REQUIRE(points[2] == geom::point_t(13, 14));
    REQUIRE(points[3] == geom::point_t(20, 10));
}

TEST_CASE("geom::distance", "[NoDB]")
{
    geom::point_t const p1{10, 10};
    geom::point_t const p2{20, 10};
    geom::point_t const p3{13, 14};

    REQUIRE(geom::distance(p1, p1) == Approx(0.0));
    REQUIRE(geom::distance(p1, p2) == Approx(10.0));
    REQUIRE(geom::distance(p1, p3) == Approx(5.0));
}

TEST_CASE("geom::interpolate", "[NoDB]")
{
    geom::point_t const p1{10, 10};
    geom::point_t const p2{20, 10};

    auto const i1 = geom::interpolate(p1, p1, 0.5);
    REQUIRE(i1.x() == 10);
    REQUIRE(i1.y() == 10);

    auto const i2 = geom::interpolate(p1, p2, 0.5);
    REQUIRE(i2.x() == 15);
    REQUIRE(i2.y() == 10);

    auto const i3 = geom::interpolate(p2, p1, 0.5);
    REQUIRE(i3.x() == 15);
    REQUIRE(i3.y() == 10);
}
