#include <catch.hpp>

#include "common-import.hpp"
#include "common-options.hpp"

static testing::db::import_t db;

static char const *const conf_file = "test_output_flex_bbox.lua";

TEST_CASE("bbox on nodes and ways in 4326")
{
    options_t const options = testing::opt_t().flex(conf_file);

    REQUIRE_NOTHROW(db.run_import(options,
                                  "n10 v1 dV Ta=b x10.0 y10.0\n"
                                  "n11 v1 dV Ta=b x10.0 y10.2\n"
                                  "n12 v1 dV Ta=b x10.2 y10.2\n"
                                  "w20 v1 dV Thighway=primary Nn10,n11,n12\n"));

    auto conn = db.db().connect();

    CHECK(3 == conn.get_count("osm2pgsql_test_points"));

    CHECK(1 == conn.get_count("osm2pgsql_test_points",
                              "ST_AsText(geom) = 'POINT(10 10)'"));

    CHECK(1 == conn.get_count(
                   "osm2pgsql_test_points",
                   "abs(min_x - 10.0) < 0.01 AND abs(min_y - 10.0) < 0.01 AND "
                   "abs(max_x - 10.0) < 0.01 AND abs(max_y - 10.0) < 0.01"));

    CHECK(1 == conn.get_count("osm2pgsql_test_highways"));

    CHECK(1 == conn.get_count(
                   "osm2pgsql_test_highways",
                   "ST_AsText(geom) = 'LINESTRING(10 10,10 10.2,10.2 10.2)'"));

    CHECK(1 == conn.get_count(
                   "osm2pgsql_test_highways",
                   "abs(min_x - 10.0) < 0.01 AND abs(min_y - 10.0) < 0.01 AND "
                   "abs(max_x - 10.2) < 0.01 AND abs(max_y - 10.2) < 0.01"));
}

TEST_CASE("bbox on nodes and ways in 3857")
{
    options_t const options = testing::opt_t().flex(conf_file);

    REQUIRE_NOTHROW(db.run_import(options,
                                  "n10 v1 dV Ta=b x10.0 y10.0\n"
                                  "n11 v1 dV Ta=b x10.0 y10.2\n"
                                  "n12 v1 dV Ta=b x10.2 y10.2\n"
                                  "w20 v1 dV Thighway=primary Nn10,n11,n12\n"));

    auto conn = db.db().connect();

    CHECK(3 == conn.get_count("osm2pgsql_test_points"));

    CHECK(1 == conn.get_count(
                   "osm2pgsql_test_points",
                   "abs(min_x - 10.0) < 0.01 AND abs(min_y - 10.0) < 0.01 AND "
                   "abs(max_x - 10.0) < 0.01 AND abs(max_y - 10.0) < 0.01"));

    CHECK(1 == conn.get_count("osm2pgsql_test_highways"));

    CHECK(1 == conn.get_count(
                   "osm2pgsql_test_highways",
                   "abs(min_x - 10.0) < 0.01 AND abs(min_y - 10.0) < 0.01 AND "
                   "abs(max_x - 10.2) < 0.01 AND abs(max_y - 10.2) < 0.01"));
}
