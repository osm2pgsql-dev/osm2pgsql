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

#include <array>

TEST_CASE("multipoint_t with a single point", "[NoDB]")
{
    geom::point_t const expected{1, 1};
    geom::point_t point = expected;

    geom::geometry_t geom{geom::multipoint_t{}};
    auto &mp = geom.get<geom::multipoint_t>();
    mp.add_geometry({1, 1});

    REQUIRE(geom.is_multipoint());
    REQUIRE(geometry_type(geom) == "MULTIPOINT");
    REQUIRE(num_geometries(geom) == 1);
    REQUIRE(area(geom) == Approx(0.0));
    REQUIRE(centroid(geom) == geom::geometry_t{std::move(point)});

    REQUIRE(mp[0] == expected);
}

TEST_CASE("multipoint_t with several points", "[NoDB]")
{
    geom::point_t const p0{1, 1};
    geom::point_t const p1{2, 1};
    geom::point_t const p2{3, 1};

    geom::geometry_t geom{geom::multipoint_t{}};
    auto &mp = geom.get<geom::multipoint_t>();
    mp.add_geometry({1, 1});
    mp.add_geometry({2, 1});
    mp.add_geometry({3, 1});

    REQUIRE(geom.is_multipoint());
    REQUIRE(geometry_type(geom) == "MULTIPOINT");
    REQUIRE(num_geometries(geom) == 3);
    REQUIRE(area(geom) == Approx(0.0));
    REQUIRE(centroid(geom) == geom::geometry_t{geom::point_t{2, 1}});

    REQUIRE(mp[0] == p0);
    REQUIRE(mp[1] == p1);
    REQUIRE(mp[2] == p2);
}
