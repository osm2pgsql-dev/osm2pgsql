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
#include "reprojection.hpp"

static void check(geom::point_t a, geom::point_t b)
{
    REQUIRE(a.x() == Approx(b.x()));
    REQUIRE(a.y() == Approx(b.y()));
}

double const X55 = 612257.1993630046;  // lon 5.5
double const Y44 = 490287.90003313165; // lat 4.4
double const X33 = 367354.31961780274; // lon 3.3
double const Y22 = 244963.0806270098;  // lat 2.2

double const X0 = 0;                  // lon 0.0
double const Y0 = 0;                  // lat 0.0
double const X1 = 111319.49079327357; // lon 1.0
double const Y1 = 111325.14286638486; // lat 1.0

double const X01 = 11131.949079327358; // lon 0.1
double const Y01 = 11131.954730972562; // lat 0.1
double const X09 = 100187.54171394622; // lon 0.9
double const Y09 = 100191.66201561989; // lat 0.9

TEST_CASE("Transform geom::null_t", "[NoDB]")
{
    auto const &reprojection =
        reprojection::create_projection(PROJ_SPHERE_MERC);

    geom::geometry_t const geom{};
    auto const result = geom::transform(geom, *reprojection);
    REQUIRE(result.is_null());
    REQUIRE(result.srid() == 3857);
}

TEST_CASE("Transform geom::point_t", "[NoDB]")
{
    auto const &reprojection =
        reprojection::create_projection(PROJ_SPHERE_MERC);

    geom::geometry_t const geom{geom::point_t{5.5, 4.4}};
    auto const result = geom::transform(geom, *reprojection);
    REQUIRE(result.is_point());
    REQUIRE(result.srid() == 3857);

    check(result.get<geom::point_t>(),
          geom::point_t{612257.1993630046, 490287.90003313165});
}

TEST_CASE("Transform geom::linestring_t", "[NoDB]")
{
    auto const &reprojection =
        reprojection::create_projection(PROJ_SPHERE_MERC);

    geom::geometry_t const geom{geom::linestring_t{{5.5, 4.4}, {3.3, 2.2}}};
    auto const result = geom::transform(geom, *reprojection);
    REQUIRE(result.is_linestring());
    REQUIRE(result.srid() == 3857);

    auto const &r = result.get<geom::linestring_t>();
    check(r[0], geom::point_t{X55, Y44});
    check(r[1], geom::point_t{X33, Y22});
}

TEST_CASE("Transform geom::polygon_t", "[NoDB]")
{
    auto const &reprojection =
        reprojection::create_projection(PROJ_SPHERE_MERC);

    geom::geometry_t geom{
        geom::polygon_t{geom::ring_t{{0, 0}, {0, 1}, {1, 1}, {1, 0}, {0, 0}}}};
    geom.get<geom::polygon_t>().add_inner_ring(geom::ring_t{
        {0.1, 0.1}, {0.1, 0.9}, {0.9, 0.9}, {0.9, 0.1}, {0.1, 0.1}});

    auto const result = geom::transform(geom, *reprojection);
    REQUIRE(result.is_polygon());
    REQUIRE(result.srid() == 3857);

    auto const &polygon = result.get<geom::polygon_t>();
    auto const &outer = polygon.outer();

    REQUIRE(outer.size() == 5);
    check(outer[0], geom::point_t{X0, Y0});
    check(outer[1], geom::point_t{X0, Y1});
    check(outer[2], geom::point_t{X1, Y1});
    check(outer[3], geom::point_t{X1, Y0});
    check(outer[4], geom::point_t{X0, Y0});

    REQUIRE(polygon.inners().size() == 1);
    auto const &inner = polygon.inners()[0];
    REQUIRE(inner.size() == 5);
    check(inner[0], geom::point_t{X01, Y01});
    check(inner[1], geom::point_t{X01, Y09});
    check(inner[2], geom::point_t{X09, Y09});
    check(inner[3], geom::point_t{X09, Y01});
    check(inner[4], geom::point_t{X01, Y01});
}

TEST_CASE("Transform geom::multipoint_t", "[NoDB]")
{
    auto const &reprojection =
        reprojection::create_projection(PROJ_SPHERE_MERC);

    geom::geometry_t geom{geom::multipoint_t{}};
    auto &mp = geom.get<geom::multipoint_t>();
    mp.add_geometry({5.5, 4.4});
    mp.add_geometry({3.3, 2.2});

    auto const result = geom::transform(geom, *reprojection);
    REQUIRE(result.is_multipoint());
    REQUIRE(result.srid() == 3857);

    auto const &rmp = result.get<geom::multipoint_t>();
    REQUIRE(rmp.num_geometries() == 2);
    check(rmp[0], geom::point_t{X55, Y44});
    check(rmp[1], geom::point_t{X33, Y22});
}

TEST_CASE("Transform geom::multilinestring_t", "[NoDB]")
{
    auto const &reprojection =
        reprojection::create_projection(PROJ_SPHERE_MERC);

    geom::geometry_t geom{geom::multilinestring_t{}};
    auto &ml = geom.get<geom::multilinestring_t>();
    ml.add_geometry(geom::linestring_t{{0, 0}, {5.5, 4.4}});
    ml.add_geometry(geom::linestring_t{{0, 0}, {3.3, 2.2}});

    auto const result = geom::transform(geom, *reprojection);
    REQUIRE(result.is_multilinestring());
    REQUIRE(result.srid() == 3857);

    auto const &rml = result.get<geom::multilinestring_t>();
    REQUIRE(rml.num_geometries() == 2);

    REQUIRE(rml[0].size() == 2);
    check(rml[0][0], geom::point_t{X0, Y0});
    check(rml[0][1], geom::point_t{X55, Y44});

    REQUIRE(rml[1].size() == 2);
    check(rml[1][0], geom::point_t{X0, Y0});
    check(rml[1][1], geom::point_t{X33, Y22});
}

TEST_CASE("Transform geom::multipolygon_t", "[NoDB]")
{
    auto const &reprojection =
        reprojection::create_projection(PROJ_SPHERE_MERC);

    geom::geometry_t geom{geom::multipolygon_t{}};
    auto &mp = geom.get<geom::multipolygon_t>();
    mp.add_geometry(
        geom::polygon_t{geom::ring_t{{0, 0}, {0, 1}, {1, 1}, {1, 0}, {0, 0}}});
    mp.add_geometry(geom::polygon_t{geom::ring_t{
        {0.1, 0.1}, {0.1, 0.9}, {0.9, 0.9}, {0.9, 0.1}, {0.1, 0.1}}});

    auto const result = geom::transform(geom, *reprojection);
    REQUIRE(result.is_multipolygon());
    REQUIRE(result.srid() == 3857);

    auto const &rmp = result.get<geom::multipolygon_t>();
    REQUIRE(rmp.num_geometries() == 2);

    auto const &first = rmp[0].outer();
    REQUIRE(first.size() == 5);
    check(first[0], geom::point_t{X0, Y0});
    check(first[1], geom::point_t{X0, Y1});
    check(first[2], geom::point_t{X1, Y1});
    check(first[3], geom::point_t{X1, Y0});
    check(first[4], geom::point_t{X0, Y0});

    auto const &second = rmp[1].outer();
    REQUIRE(second.size() == 5);
    check(second[0], geom::point_t{X01, Y01});
    check(second[1], geom::point_t{X01, Y09});
    check(second[2], geom::point_t{X09, Y09});
    check(second[3], geom::point_t{X09, Y01});
    check(second[4], geom::point_t{X01, Y01});
}

TEST_CASE("Transform geom::collection_t", "[NoDB]")
{
    auto const &reprojection =
        reprojection::create_projection(PROJ_SPHERE_MERC);

    geom::geometry_t geom{geom::collection_t{}};
    auto &c = geom.get<geom::collection_t>();
    c.add_geometry(geom::geometry_t{geom::point_t{5.5, 4.4}});
    c.add_geometry(
        geom::geometry_t{geom::linestring_t{{0.0, 0.0}, {5.5, 4.4}}});
    c.add_geometry(geom::geometry_t{
        geom::polygon_t{geom::ring_t{{0, 0}, {0, 1}, {1, 1}, {1, 0}, {0, 0}}}});

    {
        geom::geometry_t mpgeom{geom::multipoint_t{}};
        auto &mp = mpgeom.get<geom::multipoint_t>();
        mp.add_geometry({5.5, 4.4});
        mp.add_geometry({3.3, 2.2});
        c.add_geometry(std::move(mpgeom));
    }

    auto const result = geom::transform(geom, *reprojection);
    REQUIRE(result.is_collection());
    REQUIRE(result.srid() == 3857);

    auto const &rc = result.get<geom::collection_t>();
    REQUIRE(rc.num_geometries() == 4);

    REQUIRE(rc[0].is_point());
    REQUIRE(rc[0].srid() == 0);
    auto const &rc0 = rc[0].get<geom::point_t>();
    check(rc0, geom::point_t{X55, Y44});

    REQUIRE(rc[1].is_linestring());
    REQUIRE(rc[1].srid() == 0);
    auto const &rc1 = rc[1].get<geom::linestring_t>();
    REQUIRE(rc1.size() == 2);
    check(rc1[0], geom::point_t{X0, Y0});
    check(rc1[1], geom::point_t{X55, Y44});

    REQUIRE(rc[2].is_polygon());
    REQUIRE(rc[2].srid() == 0);
    auto const &rc2 = rc[2].get<geom::polygon_t>();
    REQUIRE(rc2.outer().size() == 5);
    check(rc2.outer()[0], geom::point_t{X0, Y0});
    check(rc2.outer()[1], geom::point_t{X0, Y1});
    check(rc2.outer()[2], geom::point_t{X1, Y1});
    check(rc2.outer()[3], geom::point_t{X1, Y0});
    check(rc2.outer()[4], geom::point_t{X0, Y0});

    REQUIRE(rc[3].is_multipoint());
    REQUIRE(rc[3].srid() == 0);
    auto const &rc3 = rc[3].get<geom::multipoint_t>();
    REQUIRE(rc3.num_geometries() == 2);
    check(rc3[0], geom::point_t{X55, Y44});
    check(rc3[1], geom::point_t{X33, Y22});
}
