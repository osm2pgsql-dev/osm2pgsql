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

static testing::db::import_t db;

static char const *const conf_file_geometry =
    "test_output_flex_multigeom_geometry.lua";
static char const *const conf_file_geometry_split =
    "test_output_flex_multigeom_geometry_split.lua";
static char const *const conf_file_polygon =
    "test_output_flex_multigeom_polygon.lua";
static char const *const conf_file_multipolygon =
    "test_output_flex_multigeom_multipolygon.lua";
static char const *const conf_file_multipolygon_split =
    "test_output_flex_multigeom_multipolygon_split.lua";
static char const *const data_file = "test_output_flex_multigeom.osm";

TEST_CASE("Use 'geometry' column for area (not splitting multipolygons)")
{
    options_t const options = testing::opt_t().flex(conf_file_geometry);
    REQUIRE_NOTHROW(db.run_file(options, data_file));

    auto conn = db.db().connect();

    CHECK(3 == conn.get_count("osm2pgsql_test_polygon"));
    CHECK(2 == conn.get_count("osm2pgsql_test_polygon",
                              "ST_GeometryType(geom) = 'ST_Polygon'"));
    CHECK(1 == conn.get_count("osm2pgsql_test_polygon",
                              "ST_GeometryType(geom) = 'ST_MultiPolygon'"));
    CHECK(1 == conn.get_count("osm2pgsql_test_polygon", "osm_id = 20"));
    CHECK(1 == conn.get_count("osm2pgsql_test_polygon", "osm_id = -30"));
    CHECK(1 == conn.get_count("osm2pgsql_test_polygon", "osm_id = -31"));
}

TEST_CASE("Use 'geometry' column for area (splitting multipolygons)")
{
    options_t const options = testing::opt_t().flex(conf_file_geometry_split);
    REQUIRE_NOTHROW(db.run_file(options, data_file));

    auto conn = db.db().connect();

    CHECK(4 == conn.get_count("osm2pgsql_test_polygon"));
    CHECK(4 == conn.get_count("osm2pgsql_test_polygon",
                              "ST_GeometryType(geom) = 'ST_Polygon'"));
    CHECK(0 == conn.get_count("osm2pgsql_test_polygon",
                              "ST_GeometryType(geom) = 'ST_MultiPolygon'"));
    CHECK(1 == conn.get_count("osm2pgsql_test_polygon", "osm_id = 20"));
    CHECK(1 == conn.get_count("osm2pgsql_test_polygon", "osm_id = -30"));
    CHECK(2 == conn.get_count("osm2pgsql_test_polygon", "osm_id = -31"));
}

TEST_CASE("Use 'polygon' column for area (splitting multipolygons)")
{
    options_t const options = testing::opt_t().flex(conf_file_polygon);
    REQUIRE_NOTHROW(db.run_file(options, data_file));

    auto conn = db.db().connect();

    CHECK(4 == conn.get_count("osm2pgsql_test_polygon"));
    CHECK(4 == conn.get_count("osm2pgsql_test_polygon",
                              "ST_GeometryType(geom) = 'ST_Polygon'"));
    CHECK(0 == conn.get_count("osm2pgsql_test_polygon",
                              "ST_GeometryType(geom) = 'ST_MultiPolygon'"));
    CHECK(1 == conn.get_count("osm2pgsql_test_polygon", "osm_id = 20"));
    CHECK(1 == conn.get_count("osm2pgsql_test_polygon", "osm_id = -30"));
    CHECK(2 == conn.get_count("osm2pgsql_test_polygon", "osm_id = -31"));
}

TEST_CASE("Use 'multipolygon' column for area (not splitting multipolygons)")
{
    options_t const options = testing::opt_t().flex(conf_file_multipolygon);
    REQUIRE_NOTHROW(db.run_file(options, data_file));

    auto conn = db.db().connect();

    CHECK(3 == conn.get_count("osm2pgsql_test_polygon"));
    CHECK(0 == conn.get_count("osm2pgsql_test_polygon",
                              "ST_GeometryType(geom) = 'ST_Polygon'"));
    CHECK(3 == conn.get_count("osm2pgsql_test_polygon",
                              "ST_GeometryType(geom) = 'ST_MultiPolygon'"));
    CHECK(1 == conn.get_count("osm2pgsql_test_polygon", "osm_id = 20"));
    CHECK(1 == conn.get_count("osm2pgsql_test_polygon", "osm_id = -30"));
    CHECK(1 == conn.get_count("osm2pgsql_test_polygon", "osm_id = -31"));
}

TEST_CASE("Use 'multipolygon' column for area (splitting multipolygons)")
{
    options_t const options =
        testing::opt_t().flex(conf_file_multipolygon_split);
    REQUIRE_NOTHROW(db.run_file(options, data_file));

    auto conn = db.db().connect();

    CHECK(4 == conn.get_count("osm2pgsql_test_polygon"));
    CHECK(0 == conn.get_count("osm2pgsql_test_polygon",
                              "ST_GeometryType(geom) = 'ST_Polygon'"));
    CHECK(4 == conn.get_count("osm2pgsql_test_polygon",
                              "ST_GeometryType(geom) = 'ST_MultiPolygon'"));
    CHECK(1 == conn.get_count("osm2pgsql_test_polygon", "osm_id = 20"));
    CHECK(1 == conn.get_count("osm2pgsql_test_polygon", "osm_id = -30"));
    CHECK(2 == conn.get_count("osm2pgsql_test_polygon", "osm_id = -31"));
}
