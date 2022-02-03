/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <catch.hpp>

#include "common-import.hpp"
#include "common-options.hpp"

#include "format.hpp"

#include <array>

static testing::db::import_t db;

static char const *const data_file = "test_output_flex_area.osm";

// Area values for 4326, 3857, 25832 (ETRS89 / UTM zone 32N).
// First is for polygon, second for multipolygon.
static const double area[3][2] = {{0.01, 0.08},
                                  {192987010.0, 1547130000.0},
                                  {79600737.5375453234, 635499542.9545904398}};

static std::array<char const *, 3> const names = {"4326", "3857", "25832"};

// Indexes into area arrays above
enum projs
{
    p4326 = 0,
    p3857 = 1,
    p25832 = 2
};

void check(int p1, int p2)
{
    std::string conf_file{
        "test_output_flex_area_{}_{}.lua"_format(names[p1], names[p2])};

    options_t const options = testing::opt_t().flex(conf_file.c_str());
    REQUIRE_NOTHROW(db.run_file(options, data_file));

    auto conn = db.db().connect();

    REQUIRE(2 == conn.get_count("osm2pgsql_test_polygon"));

    // polygon
    conn.assert_double(
        area[p1][0],
        "SELECT ST_Area(geom) FROM osm2pgsql_test_polygon WHERE name='poly'");
    conn.assert_double(
        area[p2][0],
        "SELECT area FROM osm2pgsql_test_polygon WHERE name='poly'");
    conn.assert_double(area[p4326][0],
                       "SELECT ST_Area(ST_Transform(geom, 4326)) "
                       "FROM osm2pgsql_test_polygon WHERE name='poly'");

    // multipolygon
    conn.assert_double(
        area[p1][1],
        "SELECT ST_Area(geom) FROM osm2pgsql_test_polygon WHERE name='multi'");
    conn.assert_double(
        area[p2][1],
        "SELECT area FROM osm2pgsql_test_polygon WHERE name='multi'");
    conn.assert_double(area[p4326][1],
                       "SELECT ST_Area(ST_Transform(geom, 4326)) "
                       "FROM osm2pgsql_test_polygon WHERE name='multi'");
}

TEST_CASE("area and area calculation in latlon (4326) projection")
{
    check(p4326, p4326);
}

TEST_CASE("area in mercator with area calculation in latlon")
{
    check(p4326, p3857);
}

#ifdef HAVE_GENERIC_PROJ
TEST_CASE("area in latlon with area calculation in 25832 projection")
{
    check(p4326, p25832);
}
#endif

TEST_CASE("area in latlon with area calculation in mercator projection")
{
    check(p3857, p4326);
}

TEST_CASE("area and area calculation in default (3857) projection")
{
    check(p3857, p3857);
}

#ifdef HAVE_GENERIC_PROJ
TEST_CASE("area in mercator with area calculation in 25832 projection")
{
    check(p3857, p25832);
}

TEST_CASE("area in 25832 with area calculation in latlon projection")
{
    check(p25832, p4326);
}

TEST_CASE("area in 25832 with area calculation in mercator projection")
{
    check(p25832, p3857);
}

TEST_CASE("area and area calculation in 25832 projection")
{
    check(p25832, p25832);
}
#endif
