#include <catch.hpp>

#include "common-import.hpp"
#include "common-options.hpp"

static testing::db::import_t db;

static char const *const conf_file = "test_output_flex.lua";

static void require_tables(pg::conn_t const &conn)
{
    conn.require_has_table("osm2pgsql_test_point");
    conn.require_has_table("osm2pgsql_test_line");
    conn.require_has_table("osm2pgsql_test_polygon");
    conn.require_has_table("osm2pgsql_test_route");
}

TEST_CASE("liechtenstein slim regression simple")
{
    options_t const options = testing::opt_t().slim().flex(conf_file);

    REQUIRE_NOTHROW(db.run_file(options, "liechtenstein-2013-08-03.osm.pbf"));

    auto conn = db.db().connect();
    require_tables(conn);

    CHECK(1362 == conn.get_count("osm2pgsql_test_point"));
    CHECK(2932 == conn.get_count("osm2pgsql_test_line"));
    CHECK(4136 == conn.get_count("osm2pgsql_test_polygon"));
    CHECK(35 == conn.get_count("osm2pgsql_test_route"));

    // Check size of lines
    conn.assert_double(
        1696.04,
        "SELECT ST_Length(geom) FROM osm2pgsql_test_line WHERE osm_id = 1101");
    conn.assert_double(1151.26,
                       "SELECT ST_Length(ST_Transform(geom,4326)::geography) "
                       "FROM osm2pgsql_test_line WHERE osm_id = 1101");

    conn.assert_double(
        311.289, "SELECT area FROM osm2pgsql_test_polygon WHERE osm_id = 3265");
    conn.assert_double(
        311.289,
        "SELECT ST_Area(geom) FROM osm2pgsql_test_polygon WHERE osm_id = 3265");
    conn.assert_double(
        143.845, "SELECT ST_Area(ST_Transform(geom,4326)::geography) FROM "
                 "osm2pgsql_test_polygon WHERE osm_id = 3265");

    // Check a point's location
    REQUIRE(1 == conn.get_count("osm2pgsql_test_point",
                                "ST_DWithin(geom, 'SRID=3857;POINT(1062645.12 "
                                "5972593.4)'::geometry, 0.1)"));
}

TEST_CASE("liechtenstein slim latlon")
{
    options_t const options =
        testing::opt_t().slim().flex(conf_file).srs(PROJ_LATLONG);

    REQUIRE_NOTHROW(db.run_file(options, "liechtenstein-2013-08-03.osm.pbf"));

    auto conn = db.db().connect();
    require_tables(conn);

    REQUIRE(1362 == conn.get_count("osm2pgsql_test_point"));
    REQUIRE(2932 == conn.get_count("osm2pgsql_test_line"));
    REQUIRE(4136 == conn.get_count("osm2pgsql_test_polygon"));

    // Check size of lines
    conn.assert_double(
        0.0105343,
        "SELECT ST_Length(geom) FROM osm2pgsql_test_line WHERE osm_id = 1101");
    conn.assert_double(1151.26,
                       "SELECT ST_Length(ST_Transform(geom,4326)::geography) "
                       "FROM osm2pgsql_test_line WHERE osm_id = 1101");

    conn.assert_double(
        1.70718e-08,
        "SELECT area FROM osm2pgsql_test_polygon WHERE osm_id = 3265");
    conn.assert_double(
        1.70718e-08,
        "SELECT ST_Area(geom) FROM osm2pgsql_test_polygon WHERE osm_id = 3265");
    conn.assert_double(
        143.845, "SELECT ST_Area(ST_Transform(geom,4326)::geography) FROM "
                 "osm2pgsql_test_polygon WHERE osm_id = 3265");

    // Check a point's location
    REQUIRE(1 == conn.get_count("osm2pgsql_test_point",
                                "ST_DWithin(geom, 'SRID=4326;POINT(9.5459035 "
                                "47.1866494)'::geometry, 0.00001)"));
}

TEST_CASE("way area slim flatnode")
{
    options_t const options =
        testing::opt_t().slim().flex(conf_file).flatnodes();

    REQUIRE_NOTHROW(db.run_file(options, "test_output_pgsql_way_area.osm"));

    auto conn = db.db().connect();
    require_tables(conn);

    REQUIRE(0 == conn.get_count("osm2pgsql_test_point"));
    REQUIRE(0 == conn.get_count("osm2pgsql_test_line"));
    REQUIRE(1 == conn.get_count("osm2pgsql_test_polygon"));
}

TEST_CASE("route relation slim flatnode")
{
    options_t const options =
        testing::opt_t().slim().flex(conf_file).flatnodes();

    REQUIRE_NOTHROW(db.run_file(options, "test_output_pgsql_route_rel.osm"));

    auto conn = db.db().connect();
    require_tables(conn);

    REQUIRE(0 == conn.get_count("osm2pgsql_test_point"));
    REQUIRE(1 == conn.get_count("osm2pgsql_test_line"));
    REQUIRE(0 == conn.get_count("osm2pgsql_test_polygon"));
    REQUIRE(1 == conn.get_count("osm2pgsql_test_route"));
}

TEST_CASE("liechtenstein slim bz2 parsing regression")
{
    options_t const options = testing::opt_t().slim().flex(conf_file);

    REQUIRE_NOTHROW(db.run_file(options, "liechtenstein-2013-08-03.osm.bz2"));

    auto conn = db.db().connect();
    require_tables(conn);

    REQUIRE(1362 == conn.get_count("osm2pgsql_test_point"));
    REQUIRE(2932 == conn.get_count("osm2pgsql_test_line"));
    REQUIRE(4136 == conn.get_count("osm2pgsql_test_polygon"));
    REQUIRE(35 == conn.get_count("osm2pgsql_test_route"));
}
