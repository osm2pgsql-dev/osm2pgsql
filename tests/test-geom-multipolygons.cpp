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

TEST_CASE("multipolygon geometry with single outer, no inner", "[NoDB]")
{
    geom::geometry_t geom{geom::multipolygon_t{}};
    auto &mp = geom.get<geom::multipolygon_t>();

    mp.add_geometry(
        geom::polygon_t{geom::ring_t{{0, 0}, {0, 1}, {1, 1}, {1, 0}, {0, 0}}});

    REQUIRE(geometry_type(geom) == "MULTIPOLYGON");
    REQUIRE(dimension(geom) == 2);
    REQUIRE(num_geometries(geom) == 1);
    REQUIRE(area(geom) == Approx(1.0));
    REQUIRE(length(geom) == Approx(0.0));
    REQUIRE(centroid(geom) == geom::geometry_t{geom::point_t{0.5, 0.5}});
    REQUIRE(geometry_n(geom, 1) ==
            geom::geometry_t{geom::polygon_t{
                geom::ring_t{{0, 0}, {0, 1}, {1, 1}, {1, 0}, {0, 0}}}});
}

TEST_CASE("multipolygon geometry with two polygons", "[NoDB]")
{
    geom::geometry_t geom{geom::multipolygon_t{}};
    auto &mp = geom.get<geom::multipolygon_t>();

    mp.add_geometry(
        geom::polygon_t{geom::ring_t{{0, 0}, {0, 1}, {1, 1}, {1, 0}, {0, 0}}});

    geom::polygon_t polygon{
        geom::ring_t{{2, 2}, {2, 5}, {5, 5}, {5, 2}, {2, 2}}};
    polygon.add_inner_ring(
        geom::ring_t{{3, 3}, {4, 3}, {4, 4}, {3, 4}, {3, 3}});
    REQUIRE(polygon.num_geometries() == 1);
    REQUIRE(polygon.inners().size() == 1);

    mp.add_geometry(std::move(polygon));

    REQUIRE(geometry_type(geom) == "MULTIPOLYGON");
    REQUIRE(dimension(geom) == 2);
    REQUIRE(num_geometries(geom) == 2);
    REQUIRE(area(geom) == Approx(9.0));
    REQUIRE(length(geom) == Approx(0.0));
}

TEST_CASE("create_multipolygon creates simple polygon from OSM data", "[NoDB]")
{
    test_buffer_t buffer;
    buffer.add_way("w20 Nn1x1y1,n2x2y1,n3x2y2,n4x1y2");
    buffer.add_way("w21 Nn4x1y2,n1x1y1");
    auto const &relation = buffer.add_relation("r30 Mw20@,w21@");

    auto const geom = geom::create_multipolygon(relation, buffer.buffer());

    REQUIRE(geom.is_polygon());
    REQUIRE(geometry_type(geom) == "POLYGON");
    REQUIRE(dimension(geom) == 2);
    REQUIRE(num_geometries(geom) == 1);
    REQUIRE(area(geom) == Approx(1.0));
    REQUIRE(length(geom) == Approx(0.0));
    REQUIRE(
        geom.get<geom::polygon_t>() ==
        geom::polygon_t{geom::ring_t{{1, 1}, {2, 1}, {2, 2}, {1, 2}, {1, 1}}});
    REQUIRE(centroid(geom) == geom::geometry_t{geom::point_t{1.5, 1.5}});
}

TEST_CASE("create_multipolygon from OSM data", "[NoDB]")
{
    test_buffer_t buffer;
    buffer.add_way("w20 Nn1x1y1,n2x2y1,n3x2y2,n4x1y2");
    buffer.add_way("w21 Nn4x1y2,n1x1y1");
    buffer.add_way("w22 Nn5x10y10,n6x10y20,n7x20y20,n5x10y10");
    auto const &relation = buffer.add_relation("r30 Mw20@,w21@,w22@");

    auto const geom = geom::create_multipolygon(relation, buffer.buffer());

    REQUIRE(geom.is_multipolygon());
    REQUIRE(geometry_type(geom) == "MULTIPOLYGON");
    REQUIRE(num_geometries(geom) == 2);
    REQUIRE(area(geom) == Approx(51.0));
    REQUIRE(length(geom) == Approx(0.0));
}

TEST_CASE("create_multipolygon from OSM data without locations", "[NoDB]")
{
    test_buffer_t buffer;
    buffer.add_way("w20 Nn1,n2,n3,n1");

    auto const &relation = buffer.add_relation("r30 Mw20@");
    auto const geom = geom::create_multipolygon(relation, buffer.buffer());

    REQUIRE(geom.is_null());
}

TEST_CASE("create_multipolygon from invalid OSM data (single node)", "[NoDB]")
{
    test_buffer_t buffer;
    buffer.add_way("w20 Nn1x1y1");

    auto const &relation = buffer.add_relation("r30 Mw20@");
    auto const geom = geom::create_multipolygon(relation, buffer.buffer());

    REQUIRE(geom.is_null());
}

TEST_CASE("create_multipolygon from invalid OSM data (way node closed)",
          "[NoDB]")
{
    test_buffer_t buffer;
    buffer.add_way("w20 Nn1x1y1,n2x2y2");

    auto const &relation = buffer.add_relation("r30 Mw20@");
    auto const geom = geom::create_multipolygon(relation, buffer.buffer());

    REQUIRE(geom.is_null());
}

TEST_CASE("create_multipolygon from invalid OSM data (self-intersection)",
          "[NoDB]")
{
    test_buffer_t buffer;
    buffer.add_way("w20 Nn1x1y1,n2x1y2,n3x2y1,n4x2y2");
    buffer.add_way("w21 Nn4x2y2,n1x1y1");

    auto const &relation = buffer.add_relation("r30 Mw20@,w21@");
    auto const geom = geom::create_multipolygon(relation, buffer.buffer());

    REQUIRE(geom.is_null());
}
