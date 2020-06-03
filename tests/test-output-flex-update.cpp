#include <catch.hpp>

#include "common-import.hpp"
#include "common-options.hpp"

static testing::db::import_t db;

TEST_CASE("updating a node")
{
    // import a node...
    REQUIRE_NOTHROW(
        db.run_import(testing::opt_t().slim().flex("test_output_flex.lua"),
                      "n10 v1 dV x10 y10\n"));

    auto conn = db.db().connect();

    REQUIRE(0 == conn.get_count("osm2pgsql_test_point"));

    // give the node a tag...
    REQUIRE_NOTHROW(db.run_import(
        testing::opt_t().slim().append().flex("test_output_flex.lua"),
        "n10 v2 dV x10 y10 Tamenity=restaurant\n"));

    REQUIRE(1 == conn.get_count("osm2pgsql_test_point"));
    REQUIRE(1 ==
            conn.get_count("osm2pgsql_test_point",
                           "node_id = 10 AND tags->'amenity' = 'restaurant'"));

    SECTION("remove the tag from node")
    {
        REQUIRE_NOTHROW(db.run_import(
            testing::opt_t().slim().append().flex("test_output_flex.lua"),
            "n10 v3 dV x10 y10\n"));
    }

    SECTION("delete the node")
    {
        REQUIRE_NOTHROW(db.run_import(
            testing::opt_t().slim().append().flex("test_output_flex.lua"),
            "n10 v3 dD\n"));
    }

    REQUIRE(0 == conn.get_count("osm2pgsql_test_point"));
}

TEST_CASE("updating a way")
{
    // import a simple way...
    REQUIRE_NOTHROW(
        db.run_import(testing::opt_t().slim().flex("test_output_flex.lua"),
                      "n10 v1 dV x10.0 y10.1\n"
                      "n11 v1 dV x10.1 y10.2\n"
                      "w20 v1 dV Thighway=primary Nn10,n11\n"));

    auto conn = db.db().connect();

    REQUIRE(0 == conn.get_count("osm2pgsql_test_point"));
    REQUIRE(1 == conn.get_count("osm2pgsql_test_line"));
    REQUIRE(1 == conn.get_count("osm2pgsql_test_line",
                                "osm_id = 20 AND tags->'highway' = 'primary' "
                                "AND ST_NumPoints(geom) = 2"));

    // now change the way itself...
    REQUIRE_NOTHROW(db.run_import(
        testing::opt_t().slim().append().flex("test_output_flex.lua"),
        "w20 v2 dV Thighway=secondary Nn10,n11\n"));

    REQUIRE(0 == conn.get_count("osm2pgsql_test_point"));
    REQUIRE(1 == conn.get_count("osm2pgsql_test_line"));
    REQUIRE(1 == conn.get_count("osm2pgsql_test_line",
                                "osm_id = 20 AND tags->'highway' = "
                                "'secondary' AND ST_NumPoints(geom) = 2"));

    // now change a node in the way...
    REQUIRE_NOTHROW(db.run_import(
        testing::opt_t().slim().append().flex("test_output_flex.lua"),
        "n10 v2 dV x10.0 y10.3\n"));

    REQUIRE(0 == conn.get_count("osm2pgsql_test_point"));
    REQUIRE(1 == conn.get_count("osm2pgsql_test_line"));
    REQUIRE(1 == conn.get_count("osm2pgsql_test_line",
                                "osm_id = 20 AND tags->'highway' = "
                                "'secondary' AND ST_NumPoints(geom) = 2"));

    // now add a node to the way...
    REQUIRE_NOTHROW(db.run_import(
        testing::opt_t().slim().append().flex("test_output_flex.lua"),
        "n12 v1 dV x10.2 y10.1\n"
        "w20 v3 dV Thighway=residential Nn10,n11,n12\n"));

    REQUIRE(0 == conn.get_count("osm2pgsql_test_point"));
    REQUIRE(1 == conn.get_count("osm2pgsql_test_line"));
    REQUIRE(1 == conn.get_count("osm2pgsql_test_line",
                                "osm_id = 20 AND tags->'highway' = "
                                "'residential' AND ST_NumPoints(geom) = 3"));

    // now delete the way...
    REQUIRE_NOTHROW(db.run_import(
        testing::opt_t().slim().append().flex("test_output_flex.lua"),
        "w20 v4 dD\n"));

    REQUIRE(0 == conn.get_count("osm2pgsql_test_point"));
    REQUIRE(0 == conn.get_count("osm2pgsql_test_line"));
}

TEST_CASE("ways as linestrings and polygons")
{
    // import a simple way...
    REQUIRE_NOTHROW(
        db.run_import(testing::opt_t().slim().flex("test_output_flex.lua"),
                      "n10 v1 dV x10.0 y10.0\n"
                      "n11 v1 dV x10.0 y10.2\n"
                      "n12 v1 dV x10.2 y10.2\n"
                      "n13 v1 dV x10.2 y10.0\n"
                      "w20 v1 dV Tbuilding=yes Nn10,n11,n12,n13,n10\n"));

    auto conn = db.db().connect();

    REQUIRE(0 == conn.get_count("osm2pgsql_test_point"));
    REQUIRE(0 == conn.get_count("osm2pgsql_test_line"));
    REQUIRE(1 == conn.get_count("osm2pgsql_test_polygon"));
    REQUIRE(1 == conn.get_count("osm2pgsql_test_polygon",
                                "osm_id = 20 AND tags->'building' = 'yes' AND "
                                "ST_GeometryType(geom) = 'ST_Polygon'"));

    // now change the way tags...
    REQUIRE_NOTHROW(db.run_import(
        testing::opt_t().slim().append().flex("test_output_flex.lua"),
        "w20 v2 dV Thighway=secondary Nn10,n11,n12,n13,n10\n"));

    REQUIRE(0 == conn.get_count("osm2pgsql_test_point"));
    REQUIRE(1 == conn.get_count("osm2pgsql_test_line"));
    REQUIRE(1 ==
            conn.get_count("osm2pgsql_test_line",
                           "osm_id = 20 AND tags->'highway' = 'secondary' AND "
                           "ST_GeometryType(geom) = 'ST_LineString'"));
    REQUIRE(0 == conn.get_count("osm2pgsql_test_polygon"));

    // now remove a node from the way...
    REQUIRE_NOTHROW(db.run_import(
        testing::opt_t().slim().append().flex("test_output_flex.lua"),
        "w20 v3 dV Thighway=secondary Nn10,n11,n12,n13\n"));

    REQUIRE(0 == conn.get_count("osm2pgsql_test_point"));
    REQUIRE(1 == conn.get_count("osm2pgsql_test_line"));
    REQUIRE(1 ==
            conn.get_count("osm2pgsql_test_line",
                           "osm_id = 20 AND tags->'highway' = 'secondary' AND "
                           "ST_GeometryType(geom) = 'ST_LineString'"));
    REQUIRE(0 == conn.get_count("osm2pgsql_test_polygon"));

    // now change the tag back to an area tag (but the way is not closed)...
    REQUIRE_NOTHROW(db.run_import(
        testing::opt_t().slim().append().flex("test_output_flex.lua"),
        "w20 v4 dV Tbuilding=yes Nn10,n11,n12,n13\n"));

    REQUIRE(0 == conn.get_count("osm2pgsql_test_point"));
    REQUIRE(0 == conn.get_count("osm2pgsql_test_line"));
    REQUIRE(0 == conn.get_count("osm2pgsql_test_polygon"));

    // now close the way again
    REQUIRE_NOTHROW(db.run_import(
        testing::opt_t().slim().append().flex("test_output_flex.lua"),
        "w20 v5 dV Tbuilding=yes Nn10,n11,n12,n13,n10\n"));

    REQUIRE(0 == conn.get_count("osm2pgsql_test_point"));
    REQUIRE(0 == conn.get_count("osm2pgsql_test_line"));
    REQUIRE(1 == conn.get_count("osm2pgsql_test_polygon"));
    REQUIRE(1 == conn.get_count("osm2pgsql_test_polygon",
                                "osm_id = 20 AND tags->'building' = 'yes' AND "
                                "ST_GeometryType(geom) = 'ST_Polygon'"));
}

TEST_CASE("multipolygons")
{
    // import a simple multipolygon relation...
    REQUIRE_NOTHROW(
        db.run_import(testing::opt_t().slim().flex("test_output_flex.lua"),
                      "n10 v1 dV x10.0 y10.0\n"
                      "n11 v1 dV x10.0 y10.2\n"
                      "n12 v1 dV x10.2 y10.2\n"
                      "n13 v1 dV x10.2 y10.0\n"
                      "w20 v1 dV Nn10,n11,n12,n13,n10\n"
                      "r30 v1 dV Ttype=multipolygon,building=yes Mw20@\n"));

    auto conn = db.db().connect();

    REQUIRE(0 == conn.get_count("osm2pgsql_test_point"));
    REQUIRE(0 == conn.get_count("osm2pgsql_test_line"));
    REQUIRE(1 == conn.get_count("osm2pgsql_test_polygon"));
    REQUIRE(1 == conn.get_count("osm2pgsql_test_polygon",
                                "osm_id = -30 AND tags->'building' = 'yes' AND "
                                "ST_GeometryType(geom) = 'ST_Polygon'"));

    // change tags on that relation...
    REQUIRE_NOTHROW(db.run_import(
        testing::opt_t().slim().append().flex("test_output_flex.lua"),
        "r30 v2 dV Ttype=multipolygon,building=yes,name=Shed Mw20@\n"));

    REQUIRE(0 == conn.get_count("osm2pgsql_test_point"));
    REQUIRE(0 == conn.get_count("osm2pgsql_test_line"));
    REQUIRE(1 == conn.get_count("osm2pgsql_test_polygon"));
    REQUIRE(1 == conn.get_count("osm2pgsql_test_polygon",
                                "osm_id = -30 AND tags->'building' = 'yes' AND "
                                "ST_GeometryType(geom) = 'ST_Polygon'"));

    SECTION("remove relation")
    {
        REQUIRE_NOTHROW(db.run_import(
            testing::opt_t().slim().append().flex("test_output_flex.lua"),
            "r30 v3 dD\n"));
    }

    SECTION("remove multipolygon tag")
    {
        REQUIRE_NOTHROW(db.run_import(
            testing::opt_t().slim().append().flex("test_output_flex.lua"),
            "r30 v3 dV Tbuilding=yes,name=Shed Mw20@\n"));
    }

    REQUIRE(0 == conn.get_count("osm2pgsql_test_point"));
    REQUIRE(0 == conn.get_count("osm2pgsql_test_line"));
    REQUIRE(0 == conn.get_count("osm2pgsql_test_polygon"));
}
