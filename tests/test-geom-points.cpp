/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <catch.hpp>

#include "common-buffer.hpp"

#include "geom-from-osm.hpp"
#include "geom-functions.hpp"
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

TEST_CASE("create_point from OSM data", "[NoDB]")
{
    test_buffer_t buffer;
    buffer.add_node("n10 x1.1 y2.2");

    auto const geom = geom::create_point(buffer.buffer().get<osmium::Node>(0));

    REQUIRE(geom.is_point());
    REQUIRE(geometry_type(geom) == "POINT");
    REQUIRE(num_geometries(geom) == 1);
    REQUIRE(area(geom) == Approx(0.0));
    REQUIRE(centroid(geom) == geom::geometry_t{geom::point_t{1.1, 2.2}});
    REQUIRE(geometry_n(geom, 1) == geom);
    REQUIRE(reverse(geom) == geom);
    REQUIRE(geom.get<geom::point_t>() == geom::point_t{1.1, 2.2});
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
