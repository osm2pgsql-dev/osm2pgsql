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

TEST_CASE("geometry collection with point", "[NoDB]")
{
    geom::geometry_t geom{geom::collection_t{}};
    auto &c = geom.get<geom::collection_t>();

    c.add_geometry(geom::geometry_t{geom::point_t{1, 1}});

    REQUIRE(geometry_type(geom) == "GEOMETRYCOLLECTION");
    REQUIRE(dimension(geom) == 0);
    REQUIRE(num_geometries(geom) == 1);
    REQUIRE(area(geom) == Approx(0.0));
    REQUIRE(length(geom) == Approx(0.0));
    REQUIRE(centroid(geom) == geom::geometry_t{geom::point_t{1, 1}});
    REQUIRE(geometry_n(geom, 1) == geom::geometry_t{geom::point_t{1, 1}});
}

TEST_CASE("geometry collection with multipoint", "[NoDB]")
{
    geom::geometry_t mpgeom{geom::multipoint_t{}};
    auto &mp = mpgeom.get<geom::multipoint_t>();
    mp.add_geometry(geom::point_t{1, 1});
    mp.add_geometry(geom::point_t{1, 2});
    mp.add_geometry(geom::point_t{2, 1});
    mp.add_geometry(geom::point_t{2, 2});

    geom::geometry_t geom{geom::collection_t{}};
    auto &c = geom.get<geom::collection_t>();
    c.add_geometry(std::move(mpgeom));

    REQUIRE(geometry_type(geom) == "GEOMETRYCOLLECTION");
    REQUIRE(dimension(geom) == 0);
    REQUIRE(num_geometries(geom) == 1);
    REQUIRE(area(geom) == Approx(0.0));
    REQUIRE(centroid(geom) == geom::geometry_t{geom::point_t{1.5, 1.5}});
}

TEST_CASE("geometry collection with several geometries", "[NoDB]")
{
    geom::geometry_t geom{geom::collection_t{}};
    auto &c = geom.get<geom::collection_t>();

    c.add_geometry(geom::geometry_t{geom::point_t{1, 1}});
    c.add_geometry(geom::geometry_t{geom::linestring_t{{1, 1}, {2, 2}}});
    c.add_geometry(geom::geometry_t{geom::point_t{2, 2}});

    REQUIRE(geometry_type(geom) == "GEOMETRYCOLLECTION");
    REQUIRE(dimension(geom) == 1);
    REQUIRE(num_geometries(geom) == 3);
    REQUIRE(area(geom) == Approx(0.0));
    REQUIRE(length(geom) == Approx(1.41421));
    REQUIRE(centroid(geom) == geom::geometry_t{geom::point_t{1.5, 1.5}});
    REQUIRE(geometry_n(geom, 1) == geom::geometry_t{geom::point_t{1, 1}});
    REQUIRE(geometry_n(geom, 2) ==
            geom::geometry_t{geom::linestring_t{{1, 1}, {2, 2}}});
    REQUIRE(geometry_n(geom, 3) == geom::geometry_t{geom::point_t{2, 2}});
}

TEST_CASE("geometry collection with polygon", "[NoDB]")
{
    geom::geometry_t geom{geom::collection_t{}};
    auto &c = geom.get<geom::collection_t>();

    c.add_geometry(geom::geometry_t{geom::point_t{1, 1}});
    c.add_geometry(geom::geometry_t{
        geom::polygon_t{geom::ring_t{{1, 1}, {1, 2}, {2, 2}, {2, 1}, {1, 1}}}});

    REQUIRE(geometry_type(geom) == "GEOMETRYCOLLECTION");
    REQUIRE(num_geometries(geom) == 2);
    REQUIRE(area(geom) == Approx(1.0));
    REQUIRE(length(geom) == Approx(0.0));
    REQUIRE(centroid(geom) == geom::geometry_t{geom::point_t{1.5, 1.5}});
}

TEST_CASE("create_collection from OSM data", "[NoDB]")
{
    test_buffer_t buffer;
    buffer.add_node("n1 x1 y1");
    buffer.add_way("w20 Nn1x1y1,n2x2y1,n3x2y2,n4x1y2,n1x1y1");
    buffer.add_way("w21 Nn5x10y10,n6x10y11");
    buffer.add_relation("r30 Mw20@");

    auto const geom = geom::create_collection(buffer.buffer());

    REQUIRE(geometry_type(geom) == "GEOMETRYCOLLECTION");
    REQUIRE(dimension(geom) == 1);
    REQUIRE(num_geometries(geom) == 3);

    auto const &c = geom.get<geom::collection_t>();
    REQUIRE(c[0] == geom::geometry_t{geom::point_t{1, 1}});
    REQUIRE(c[1] == geom::geometry_t{geom::linestring_t{
                        {1, 1}, {2, 1}, {2, 2}, {1, 2}, {1, 1}}});
    REQUIRE(c[2] == geom::geometry_t{geom::linestring_t{{10, 10}, {10, 11}}});

    REQUIRE(area(geom) == Approx(0.0));
    REQUIRE(length(geom) == Approx(5.0));
    REQUIRE(centroid(geom) == geom::geometry_t{geom::point_t{3.2, 3.3}});
}

TEST_CASE("create_collection from no OSM data returns null geometry", "[NoDB]")
{
    test_buffer_t buffer;
    buffer.add_relation("r30 Mw20@");

    auto const geom = geom::create_collection(buffer.buffer());

    REQUIRE(geometry_type(geom) == "NULL");
    REQUIRE(dimension(geom) == 0);
    REQUIRE(num_geometries(geom) == 0);
}

TEST_CASE("create_collection from OSM data with single-node way", "[NoDB]")
{
    test_buffer_t buffer;
    buffer.add_way("w20 Nn1x1y1");

    auto const geom = geom::create_collection(buffer.buffer());

    REQUIRE(geom.is_null());
}
