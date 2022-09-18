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

TEST_CASE("polygon geometry without inner", "[NoDB]")
{
    geom::geometry_t const geom{
        geom::polygon_t{geom::ring_t{{0, 0}, {0, 1}, {1, 1}, {1, 0}, {0, 0}}}};

    REQUIRE(num_geometries(geom) == 1);
    REQUIRE(area(geom) == Approx(1.0));
    REQUIRE(geometry_type(geom) == "POLYGON");
    REQUIRE(centroid(geom) == geom::geometry_t{geom::point_t{0.5, 0.5}});
    REQUIRE(geometry_n(geom, 1) == geom);
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

    auto const geom_rev = reverse(geom);
    REQUIRE(geom_rev.is_polygon());
    auto const &rev = geom_rev.get<geom::polygon_t>();
    REQUIRE(rev.outer() ==
            geom::ring_t{{0, 0}, {3, 0}, {3, 3}, {0, 3}, {0, 0}});
    REQUIRE(rev.inners().size() == 1);
    REQUIRE(rev.inners()[0] ==
            geom::ring_t{{1, 1}, {1, 2}, {2, 2}, {2, 1}, {1, 1}});
}

TEST_CASE("create_polygon from OSM data", "[NoDB]")
{
    test_buffer_t buffer;
    buffer.add_way("w20 Nn1x1y1,n2x2y1,n3x2y2,n4x1y2,n1x1y1");

    auto const geom = geom::create_polygon(buffer.buffer().get<osmium::Way>(0));

    REQUIRE(geom.is_polygon());
    REQUIRE(geometry_type(geom) == "POLYGON");
    REQUIRE(num_geometries(geom) == 1);
    REQUIRE(area(geom) == Approx(1.0));
    REQUIRE(
        geom.get<geom::polygon_t>() ==
        geom::polygon_t{geom::ring_t{{1, 1}, {2, 1}, {2, 2}, {1, 2}, {1, 1}}});
    REQUIRE(centroid(geom) == geom::geometry_t{geom::point_t{1.5, 1.5}});
}

TEST_CASE("create_polygon from OSM data (reverse)", "[NoDB]")
{
    test_buffer_t buffer;
    buffer.add_way("w20 Nn1x1y1,n2x1y2,n3x2y2,n4x2y1,n1x1y1");

    auto const geom = geom::create_polygon(buffer.buffer().get<osmium::Way>(0));

    REQUIRE(geom.is_polygon());
    REQUIRE(geometry_type(geom) == "POLYGON");
    REQUIRE(num_geometries(geom) == 1);
    REQUIRE(area(geom) == Approx(1.0));
    REQUIRE(
        geom.get<geom::polygon_t>() ==
        geom::polygon_t{geom::ring_t{{1, 1}, {2, 1}, {2, 2}, {1, 2}, {1, 1}}});
    REQUIRE(centroid(geom) == geom::geometry_t{geom::point_t{1.5, 1.5}});
}

TEST_CASE("create_polygon from OSM data without locations", "[NoDB]")
{
    test_buffer_t buffer;
    buffer.add_way("w20 Nn1,n2,n3,n1");

    auto const geom = geom::create_polygon(buffer.buffer().get<osmium::Way>(0));

    REQUIRE(geom.is_null());
}

TEST_CASE("create_polygon from invalid OSM data (single node)", "[NoDB]")
{
    test_buffer_t buffer;
    buffer.add_way("w20 Nn1x1y1");

    auto const geom = geom::create_polygon(buffer.buffer().get<osmium::Way>(0));

    REQUIRE(geom.is_null());
}

TEST_CASE("create_polygon from invalid OSM data (way node closed)", "[NoDB]")
{
    test_buffer_t buffer;
    buffer.add_way("w20 Nn1x1y1,n2x2y2");

    auto const geom = geom::create_polygon(buffer.buffer().get<osmium::Way>(0));

    REQUIRE(geom.is_null());
}

TEST_CASE("create_polygon from invalid OSM data (self-intersection)", "[NoDB]")
{
    test_buffer_t buffer;
    buffer.add_way("w20 Nn1x1y1,n2x1y2,n3x2y1,n4x2y2,n1x1y1");

    auto const geom = geom::create_polygon(buffer.buffer().get<osmium::Way>(0));

    REQUIRE(geom.is_null());
}
