#include "catch.hpp"

#include "common-import.hpp"
#include "configs.hpp"

static testing::db::import_t db;

TEST_CASE("default projection")
{
    testing::options::slim_default options(db.db());

    REQUIRE_NOTHROW(db.run_file(options, "test_output_pgsql_area.osm"));

    auto conn = db.db().connect();

    REQUIRE(2 == conn.get_count("osm2pgsql_test_polygon"));
    conn.assert_double(1.23927e+10,
                       "SELECT way_area FROM osm2pgsql_test_polygon WHERE name='poly'");
    conn.assert_double(9.91828e+10,
                       "SELECT way_area FROM osm2pgsql_test_polygon WHERE name='multi'");
}

TEST_CASE("latlon projection")
{
    testing::options::slim_default options(db.db());
    options.projection.reset(reprojection::create_projection(PROJ_LATLONG));

    REQUIRE_NOTHROW(db.run_file(options, "test_output_pgsql_area.osm"));

    auto conn = db.db().connect();

    REQUIRE(2 == conn.get_count("osm2pgsql_test_polygon"));
    conn.assert_double(1,
                       "SELECT way_area FROM osm2pgsql_test_polygon WHERE name='poly'");
    conn.assert_double(8,
                       "SELECT way_area FROM osm2pgsql_test_polygon WHERE name='multi'");
}

TEST_CASE("latlon projection with way_area reprojection")
{
    testing::options::slim_default options(db.db());
    options.projection.reset(reprojection::create_projection(PROJ_LATLONG));
    options.reproject_area = true;

    REQUIRE_NOTHROW(db.run_file(options, "test_output_pgsql_area.osm"));

    auto conn = db.db().connect();

    REQUIRE(2 == conn.get_count("osm2pgsql_test_polygon"));
    conn.assert_double(1.23927e+10,
                       "SELECT way_area FROM osm2pgsql_test_polygon WHERE name='poly'");
    conn.assert_double(9.91828e+10,
                       "SELECT way_area FROM osm2pgsql_test_polygon WHERE name='multi'");
}
