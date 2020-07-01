#include <catch.hpp>

#include "common-import.hpp"
#include "common-options.hpp"

static testing::db::import_t db;

static char const *const conf_file = "test_output_flex_area.lua";
static char const *const data_file = "test_output_pgsql_area.osm";

TEST_CASE("area calculation in default projection")
{
    options_t const options = testing::opt_t().flex(conf_file);

    REQUIRE_NOTHROW(db.run_file(options, data_file));

    auto conn = db.db().connect();

    REQUIRE(2 == conn.get_count("osm2pgsql_test_polygon"));
    conn.assert_double(
        1.23927e+10,
        "SELECT area FROM osm2pgsql_test_polygon WHERE name='poly'");
    conn.assert_double(
        9.91828e+10,
        "SELECT area FROM osm2pgsql_test_polygon WHERE name='multi'");
}

TEST_CASE("area calculation in latlon projection")
{
    options_t const options =
        testing::opt_t().flex(conf_file).srs(PROJ_LATLONG);

    REQUIRE_NOTHROW(db.run_file(options, data_file));

    auto conn = db.db().connect();

    REQUIRE(2 == conn.get_count("osm2pgsql_test_polygon"));
    conn.assert_double(
        1.0, "SELECT area FROM osm2pgsql_test_polygon WHERE name='poly'");
    conn.assert_double(
        8.0, "SELECT area FROM osm2pgsql_test_polygon WHERE name='multi'");
}

TEST_CASE("area calculation in latlon projection with way area reprojection")
{
    options_t options = testing::opt_t().flex(conf_file).srs(PROJ_LATLONG);
    options.reproject_area = true;

    REQUIRE_NOTHROW(db.run_file(options, data_file));

    auto conn = db.db().connect();

    REQUIRE(2 == conn.get_count("osm2pgsql_test_polygon"));
    conn.assert_double(
        1.23927e+10,
        "SELECT area FROM osm2pgsql_test_polygon WHERE name='poly'");
    conn.assert_double(
        9.91828e+10,
        "SELECT area FROM osm2pgsql_test_polygon WHERE name='multi'");
}
