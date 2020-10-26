#include <catch.hpp>

#include "common-import.hpp"
#include "common-options.hpp"

static testing::db::import_t db;

static char const *const conf_file = "test_output_flex_uni.lua";

struct options_slim_default
{
    static options_t options()
    {
        return testing::opt_t().slim().flex(conf_file);
    }
};

struct options_slim_expire
{
    static options_t options()
    {
        options_t o = options_slim_default::options();
        o.expire_tiles_zoom = 10;
        return o;
    }
};

TEMPLATE_TEST_CASE("updating a node", "", options_slim_default,
                   options_slim_expire)
{
    options_t options = TestType::options();

    // import a node...
    REQUIRE_NOTHROW(db.run_import(options, "n10 v1 dV x10 y10\n"));

    auto conn = db.db().connect();

    REQUIRE(0 == conn.get_count("osm2pgsql_test_data2", "x_type = 'N'"));
    REQUIRE(0 == conn.get_count("osm2pgsql_test_data1", "the_id > 0"));

    // give the node a tag...
    options.append = true;
    REQUIRE_NOTHROW(
        db.run_import(options, "n10 v2 dV x10 y10 Tamenity=restaurant\n"));

    REQUIRE(1 == conn.get_count("osm2pgsql_test_data2", "x_type = 'N'"));
    REQUIRE(1 == conn.get_count("osm2pgsql_test_data1", "the_id > 0"));
    REQUIRE(1 == conn.get_count("osm2pgsql_test_data2",
                                "x_type = 'N' AND x_id = 10 AND "
                                "tags->'amenity' = 'restaurant'"));
    REQUIRE(1 == conn.get_count("osm2pgsql_test_data1",
                                "the_id = 10 AND "
                                "tags->'amenity' = 'restaurant'"));

    SECTION("remove the tag from node")
    {
        REQUIRE_NOTHROW(db.run_import(options, "n10 v3 dV x10 y10\n"));
    }

    SECTION("delete the node")
    {
        REQUIRE_NOTHROW(db.run_import(options, "n10 v3 dD\n"));
    }

    REQUIRE(0 == conn.get_count("osm2pgsql_test_data2", "x_type = 'N'"));
    REQUIRE(0 == conn.get_count("osm2pgsql_test_data1", "the_id > 0"));
}

TEMPLATE_TEST_CASE("updating a way", "", options_slim_default,
                   options_slim_expire)
{
    options_t options = TestType::options();

    // import a simple way...
    REQUIRE_NOTHROW(db.run_import(options,
                                  "n10 v1 dV x10.0 y10.1\n"
                                  "n11 v1 dV x10.1 y10.2\n"
                                  "w20 v1 dV Thighway=primary Nn10,n11\n"));

    auto conn = db.db().connect();

    REQUIRE(0 == conn.get_count("osm2pgsql_test_data2", "x_type = 'N'"));
    REQUIRE(0 == conn.get_count("osm2pgsql_test_data1", "the_id > 0"));
    REQUIRE(1 == conn.get_count("osm2pgsql_test_data2", "x_type = 'W'"));
    REQUIRE(1 == conn.get_count("osm2pgsql_test_data1",
                                "the_id < 0 AND the_id > -1e17"));
    REQUIRE(1 ==
            conn.get_count(
                "osm2pgsql_test_data2",
                "x_type = 'W' AND x_id = 20 AND tags->'highway' = 'primary' "
                "AND ST_NumPoints(geom) = 2"));
    REQUIRE(1 == conn.get_count("osm2pgsql_test_data1",
                                "the_id = -20 AND tags->'highway' = 'primary' "
                                "AND ST_NumPoints(geom) = 2"));

    // now change the way itself...
    options.append = true;
    REQUIRE_NOTHROW(
        db.run_import(options, "w20 v2 dV Thighway=secondary Nn10,n11\n"));

    REQUIRE(0 == conn.get_count("osm2pgsql_test_data2", "x_type = 'N'"));
    REQUIRE(0 == conn.get_count("osm2pgsql_test_data1", "the_id > 0"));
    REQUIRE(1 == conn.get_count("osm2pgsql_test_data2", "x_type = 'W'"));
    REQUIRE(1 == conn.get_count("osm2pgsql_test_data1",
                                "the_id < 0 AND the_id > -1e17"));
    REQUIRE(1 ==
            conn.get_count("osm2pgsql_test_data2",
                           "x_type = 'W' AND x_id = 20 AND tags->'highway' = "
                           "'secondary' AND ST_NumPoints(geom) = 2"));
    REQUIRE(1 == conn.get_count("osm2pgsql_test_data1",
                                "the_id = -20 AND tags->'highway' = "
                                "'secondary' AND ST_NumPoints(geom) = 2"));

    // now change a node in the way...
    REQUIRE_NOTHROW(db.run_import(options, "n10 v2 dV x10.0 y10.3\n"));

    REQUIRE(0 == conn.get_count("osm2pgsql_test_data2", "x_type = 'N'"));
    REQUIRE(0 == conn.get_count("osm2pgsql_test_data1", "the_id > 0"));
    REQUIRE(1 == conn.get_count("osm2pgsql_test_data2", "x_type = 'W'"));
    REQUIRE(1 == conn.get_count("osm2pgsql_test_data1",
                                "the_id < 0 AND the_id > -1e17"));
    REQUIRE(1 ==
            conn.get_count("osm2pgsql_test_data2",
                           "x_type = 'W' AND x_id = 20 AND tags->'highway' = "
                           "'secondary' AND ST_NumPoints(geom) = 2"));
    REQUIRE(1 == conn.get_count("osm2pgsql_test_data1",
                                "the_id = -20 AND tags->'highway' = "
                                "'secondary' AND ST_NumPoints(geom) = 2"));

    // now add a node to the way...
    REQUIRE_NOTHROW(db.run_import(
        options, "n12 v1 dV x10.2 y10.1\n"
                 "w20 v3 dV Thighway=residential Nn10,n11,n12\n"));

    REQUIRE(0 == conn.get_count("osm2pgsql_test_data2", "x_type = 'N'"));
    REQUIRE(0 == conn.get_count("osm2pgsql_test_data1", "the_id > 0"));
    REQUIRE(1 == conn.get_count("osm2pgsql_test_data2", "x_type = 'W'"));
    REQUIRE(1 == conn.get_count("osm2pgsql_test_data1",
                                "the_id < 0 AND the_id > -1e17"));
    REQUIRE(1 ==
            conn.get_count("osm2pgsql_test_data2",
                           "x_type = 'W' AND x_id = 20 AND tags->'highway' = "
                           "'residential' AND ST_NumPoints(geom) = 3"));
    REQUIRE(1 == conn.get_count("osm2pgsql_test_data1",
                                "the_id = -20 AND tags->'highway' = "
                                "'residential' AND ST_NumPoints(geom) = 3"));

    // now delete the way...
    REQUIRE_NOTHROW(db.run_import(options, "w20 v4 dD\n"));

    REQUIRE(0 == conn.get_count("osm2pgsql_test_data2", "x_type = 'N'"));
    REQUIRE(0 == conn.get_count("osm2pgsql_test_data1", "the_id > 0"));
    REQUIRE(0 == conn.get_count("osm2pgsql_test_data2", "x_type = 'W'"));
    REQUIRE(0 == conn.get_count("osm2pgsql_test_data1",
                                "the_id < 0 AND the_id > -1e17"));
}

TEMPLATE_TEST_CASE("ways as linestrings and polygons", "", options_slim_default,
                   options_slim_expire)
{
    options_t options = TestType::options();

    // import a simple way...
    REQUIRE_NOTHROW(db.run_import(
        options, "n10 v1 dV x10.0 y10.0\n"
                 "n11 v1 dV x10.0 y10.2\n"
                 "n12 v1 dV x10.2 y10.2\n"
                 "n13 v1 dV x10.2 y10.0\n"
                 "w20 v1 dV Tbuilding=yes Nn10,n11,n12,n13,n10\n"));

    auto conn = db.db().connect();

    REQUIRE(0 == conn.get_count("osm2pgsql_test_data2", "x_type != 'W'"));
    REQUIRE(0 == conn.get_count("osm2pgsql_test_data1",
                                "the_id > 0 AND the_id < -1e17"));
    REQUIRE(1 ==
            conn.get_count(
                "osm2pgsql_test_data2",
                "x_type = 'W' AND x_id = 20 AND tags->'building' = 'yes' AND "
                "ST_GeometryType(geom) = 'ST_Polygon'"));
    REQUIRE(1 == conn.get_count("osm2pgsql_test_data1",
                                "the_id = -20 AND tags->'building' = 'yes' AND "
                                "ST_GeometryType(geom) = 'ST_Polygon'"));
    REQUIRE(0 == conn.get_count("osm2pgsql_test_data2",
                                "x_type = 'W' AND x_id = 20 AND "
                                "tags->'highway' = 'secondary' AND "
                                "ST_GeometryType(geom) = 'ST_LineString'"));
    REQUIRE(0 == conn.get_count("osm2pgsql_test_data1",
                                "the_id = -20 AND "
                                "tags->'highway' = 'secondary' AND "
                                "ST_GeometryType(geom) = 'ST_LineString'"));

    // now change the way tags...
    options.append = true;
    REQUIRE_NOTHROW(db.run_import(
        options, "w20 v2 dV Thighway=secondary Nn10,n11,n12,n13,n10\n"));

    REQUIRE(0 == conn.get_count("osm2pgsql_test_data2", "x_type != 'W'"));
    REQUIRE(0 == conn.get_count("osm2pgsql_test_data1",
                                "the_id > 0 AND the_id < -1e17"));
    REQUIRE(0 ==
            conn.get_count(
                "osm2pgsql_test_data2",
                "x_type = 'W' AND x_id = 20 AND tags->'building' = 'yes' AND "
                "ST_GeometryType(geom) = 'ST_Polygon'"));
    REQUIRE(0 == conn.get_count("osm2pgsql_test_data1",
                                "the_id = -20 AND tags->'building' = 'yes' AND "
                                "ST_GeometryType(geom) = 'ST_Polygon'"));
    REQUIRE(1 == conn.get_count("osm2pgsql_test_data2",
                                "x_type = 'W' AND x_id = 20 AND "
                                "tags->'highway' = 'secondary' AND "
                                "ST_GeometryType(geom) = 'ST_LineString'"));
    REQUIRE(1 == conn.get_count("osm2pgsql_test_data1",
                                "the_id = -20 AND "
                                "tags->'highway' = 'secondary' AND "
                                "ST_GeometryType(geom) = 'ST_LineString'"));

    // now remove a node from the way...
    REQUIRE_NOTHROW(db.run_import(
        options, "w20 v3 dV Thighway=secondary Nn10,n11,n12,n13\n"));

    REQUIRE(0 == conn.get_count("osm2pgsql_test_data2", "x_type != 'W'"));
    REQUIRE(0 == conn.get_count("osm2pgsql_test_data1",
                                "the_id > 0 AND the_id < -1e17"));
    REQUIRE(0 ==
            conn.get_count(
                "osm2pgsql_test_data2",
                "x_type = 'W' AND x_id = 20 AND tags->'building' = 'yes' AND "
                "ST_GeometryType(geom) = 'ST_Polygon'"));
    REQUIRE(0 == conn.get_count("osm2pgsql_test_data1",
                                "the_id = -20 AND tags->'building' = 'yes' AND "
                                "ST_GeometryType(geom) = 'ST_Polygon'"));
    REQUIRE(1 == conn.get_count("osm2pgsql_test_data2",
                                "x_type = 'W' AND x_id = 20 AND "
                                "tags->'highway' = 'secondary' AND "
                                "ST_GeometryType(geom) = 'ST_LineString'"));
    REQUIRE(1 == conn.get_count("osm2pgsql_test_data1",
                                "the_id = -20 AND "
                                "tags->'highway' = 'secondary' AND "
                                "ST_GeometryType(geom) = 'ST_LineString'"));

    // now change the tag back to an area tag (but the way is not closed)...
    REQUIRE_NOTHROW(
        db.run_import(options, "w20 v4 dV Tbuilding=yes Nn10,n11,n12,n13\n"));

    REQUIRE(0 == conn.get_count("osm2pgsql_test_data2"));
    REQUIRE(0 == conn.get_count("osm2pgsql_test_data1"));

    // now close the way again
    REQUIRE_NOTHROW(db.run_import(
        options, "w20 v5 dV Tbuilding=yes Nn10,n11,n12,n13,n10\n"));

    REQUIRE(0 == conn.get_count("osm2pgsql_test_data2", "x_type != 'W'"));
    REQUIRE(0 == conn.get_count("osm2pgsql_test_data1",
                                "the_id > 0 AND the_id < -1e17"));
    REQUIRE(1 ==
            conn.get_count(
                "osm2pgsql_test_data2",
                "x_type = 'W' AND x_id = 20 AND tags->'building' = 'yes' AND "
                "ST_GeometryType(geom) = 'ST_Polygon'"));
    REQUIRE(1 == conn.get_count("osm2pgsql_test_data1",
                                "the_id = -20 AND tags->'building' = 'yes' AND "
                                "ST_GeometryType(geom) = 'ST_Polygon'"));
}

TEMPLATE_TEST_CASE("multipolygons", "", options_slim_default,
                   options_slim_expire)
{
    options_t options = TestType::options();

    // import a simple multipolygon relation...
    REQUIRE_NOTHROW(db.run_import(
        options, "n10 v1 dV x10.0 y10.0\n"
                 "n11 v1 dV x10.0 y10.2\n"
                 "n12 v1 dV x10.2 y10.2\n"
                 "n13 v1 dV x10.2 y10.0\n"
                 "w20 v1 dV Nn10,n11,n12,n13,n10\n"
                 "r30 v1 dV Ttype=multipolygon,building=yes Mw20@\n"));

    auto conn = db.db().connect();

    REQUIRE(0 == conn.get_count("osm2pgsql_test_data2", "x_type = 'N'"));
    REQUIRE(0 == conn.get_count("osm2pgsql_test_data1", "the_id > 0"));
    REQUIRE(0 == conn.get_count("osm2pgsql_test_data2", "x_type = 'W'"));
    REQUIRE(0 == conn.get_count("osm2pgsql_test_data1",
                                "the_id < 0 AND the_id > -1e17"));
    REQUIRE(1 == conn.get_count("osm2pgsql_test_data2", "x_type = 'R'"));
    REQUIRE(1 == conn.get_count("osm2pgsql_test_data1", "the_id < -1e17"));
    REQUIRE(1 ==
            conn.get_count(
                "osm2pgsql_test_data2",
                "x_type = 'R' AND x_id = 30 AND tags->'building' = 'yes' AND "
                "ST_GeometryType(geom) = 'ST_Polygon'"));
    REQUIRE(1 == conn.get_count(
                     "osm2pgsql_test_data1",
                     "the_id = (-30 - 1e17) AND tags->'building' = 'yes' AND "
                     "ST_GeometryType(geom) = 'ST_Polygon'"));

    // change tags on that relation...
    options.append = true;
    REQUIRE_NOTHROW(db.run_import(
        options,
        "r30 v2 dV Ttype=multipolygon,building=yes,name=Shed Mw20@\n"));

    REQUIRE(0 == conn.get_count("osm2pgsql_test_data2", "x_type = 'N'"));
    REQUIRE(0 == conn.get_count("osm2pgsql_test_data1", "the_id > 0"));
    REQUIRE(0 == conn.get_count("osm2pgsql_test_data2", "x_type = 'W'"));
    REQUIRE(0 == conn.get_count("osm2pgsql_test_data1",
                                "the_id < 0 AND the_id > -1e17"));
    REQUIRE(1 == conn.get_count("osm2pgsql_test_data2", "x_type = 'R'"));
    REQUIRE(1 == conn.get_count("osm2pgsql_test_data1", "the_id < -1e17"));
    REQUIRE(1 ==
            conn.get_count(
                "osm2pgsql_test_data2",
                "x_type = 'R' AND x_id = 30 AND tags->'building' = 'yes' AND "
                "ST_GeometryType(geom) = 'ST_Polygon'"));
    REQUIRE(1 == conn.get_count(
                     "osm2pgsql_test_data1",
                     "the_id = (-30 - 1e17) AND tags->'building' = 'yes' AND "
                     "ST_GeometryType(geom) = 'ST_Polygon'"));

    SECTION("remove relation")
    {
        REQUIRE_NOTHROW(db.run_import(options, "r30 v3 dD\n"));
    }

    SECTION("remove multipolygon tag")
    {
        REQUIRE_NOTHROW(db.run_import(
            options, "r30 v3 dV Tbuilding=yes,name=Shed Mw20@\n"));
    }

    REQUIRE(0 == conn.get_count("osm2pgsql_test_data2"));
    REQUIRE(0 == conn.get_count("osm2pgsql_test_data1"));
}
