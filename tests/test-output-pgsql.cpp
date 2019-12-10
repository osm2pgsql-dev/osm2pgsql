#include <catch.hpp>

#include "common-import.hpp"
#include "common-options.hpp"

static testing::db::import_t db;

static void require_tables(pg::conn_t const &conn)
{
    conn.require_has_table("osm2pgsql_test_point");
    conn.require_has_table("osm2pgsql_test_line");
    conn.require_has_table("osm2pgsql_test_polygon");
    conn.require_has_table("osm2pgsql_test_roads");
}

TEST_CASE("liechtenstein slim regression simple")
{
    REQUIRE_NOTHROW(db.run_file(testing::opt_t().slim(),
                                "liechtenstein-2013-08-03.osm.pbf"));

    auto conn = db.db().connect();
    require_tables(conn);

    REQUIRE(1342 == conn.get_count("osm2pgsql_test_point"));
    REQUIRE(3231 == conn.get_count("osm2pgsql_test_line"));
    REQUIRE(375 == conn.get_count("osm2pgsql_test_roads"));
    REQUIRE(4130 == conn.get_count("osm2pgsql_test_polygon"));

    // Check size of lines
    conn.assert_double(
        1696.04,
        "SELECT ST_Length(way) FROM osm2pgsql_test_line WHERE osm_id = 1101");
    conn.assert_double(1151.26,
                       "SELECT ST_Length(ST_Transform(way,4326)::geography) "
                       "FROM osm2pgsql_test_line WHERE osm_id = 1101");

    conn.assert_double(
        311.289,
        "SELECT way_area FROM osm2pgsql_test_polygon WHERE osm_id = 3265");
    conn.assert_double(
        311.289,
        "SELECT ST_Area(way) FROM osm2pgsql_test_polygon WHERE osm_id = 3265");
    conn.assert_double(143.845,
                       "SELECT ST_Area(ST_Transform(way,4326)::geography) FROM "
                       "osm2pgsql_test_polygon WHERE osm_id = 3265");

    // Check a point's location
    REQUIRE(1 == conn.get_count("osm2pgsql_test_point",
                                "ST_DWithin(way, 'SRID=3857;POINT(1062645.12 "
                                "5972593.4)'::geometry, 0.1)"));
}

TEST_CASE("liechtenstein slim latlon")
{
    REQUIRE_NOTHROW(db.run_file(testing::opt_t().slim().srs(PROJ_LATLONG),
                                "liechtenstein-2013-08-03.osm.pbf"));

    auto conn = db.db().connect();
    require_tables(conn);

    REQUIRE(1342 == conn.get_count("osm2pgsql_test_point"));
    REQUIRE(3229 == conn.get_count("osm2pgsql_test_line"));
    REQUIRE(374 == conn.get_count("osm2pgsql_test_roads"));
    REQUIRE(4130 == conn.get_count("osm2pgsql_test_polygon"));

    // Check size of lines
    conn.assert_double(
        0.0105343,
        "SELECT ST_Length(way) FROM osm2pgsql_test_line WHERE osm_id = 1101");
    conn.assert_double(1151.26,
                       "SELECT ST_Length(ST_Transform(way,4326)::geography) "
                       "FROM osm2pgsql_test_line WHERE osm_id = 1101");

    conn.assert_double(
        1.70718e-08,
        "SELECT way_area FROM osm2pgsql_test_polygon WHERE osm_id = 3265");
    conn.assert_double(
        1.70718e-08,
        "SELECT ST_Area(way) FROM osm2pgsql_test_polygon WHERE osm_id = 3265");
    conn.assert_double(143.845,
                       "SELECT ST_Area(ST_Transform(way,4326)::geography) FROM "
                       "osm2pgsql_test_polygon WHERE osm_id = 3265");

    // Check a point's location
    REQUIRE(1 == conn.get_count("osm2pgsql_test_point",
                                "ST_DWithin(way, 'SRID=4326;POINT(9.5459035 "
                                "47.1866494)'::geometry, 0.00001)"));
}

TEST_CASE("way area slim flatnode")
{
    REQUIRE_NOTHROW(db.run_file(testing::opt_t().slim().flatnodes(),
                                "test_output_pgsql_way_area.osm"));

    auto conn = db.db().connect();
    require_tables(conn);

    REQUIRE(0 == conn.get_count("osm2pgsql_test_point"));
    REQUIRE(0 == conn.get_count("osm2pgsql_test_line"));
    REQUIRE(0 == conn.get_count("osm2pgsql_test_roads"));
    REQUIRE(1 == conn.get_count("osm2pgsql_test_polygon"));
}

TEST_CASE("route relation slim flatnode")
{
    REQUIRE_NOTHROW(db.run_file(testing::opt_t().slim().flatnodes(),
                                "test_output_pgsql_route_rel.osm"));

    auto conn = db.db().connect();
    require_tables(conn);

    REQUIRE(0 == conn.get_count("osm2pgsql_test_point"));
    REQUIRE(2 == conn.get_count("osm2pgsql_test_line"));
    REQUIRE(1 == conn.get_count("osm2pgsql_test_roads"));
    REQUIRE(0 == conn.get_count("osm2pgsql_test_polygon"));
}

TEST_CASE("liechtenstein slim bz2 parsing regression")
{
    REQUIRE_NOTHROW(db.run_file(testing::opt_t().slim(),
                                "liechtenstein-2013-08-03.osm.bz2"));

    auto conn = db.db().connect();
    require_tables(conn);

    REQUIRE(1342 == conn.get_count("osm2pgsql_test_point"));
    REQUIRE(3231 == conn.get_count("osm2pgsql_test_line"));
    REQUIRE(375 == conn.get_count("osm2pgsql_test_roads"));
    REQUIRE(4130 == conn.get_count("osm2pgsql_test_polygon"));
}
