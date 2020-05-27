#include <catch.hpp>

#include "common-import.hpp"
#include "common-options.hpp"

static testing::db::import_t db;

TEST_CASE("nodes and ways")
{
    testing::opt_t options = testing::opt_t()
                                 .slim()
                                 .flex("test_output_flex_extra.lua")
                                 .srs(PROJ_LATLONG);

    REQUIRE_NOTHROW(db.run_import(options,
                                  "n10 v1 dV x10.0 y10.0\n"
                                  "n11 v1 dV x10.0 y10.2\n"
                                  "n12 v1 dV x10.2 y10.2\n"
                                  "n13 v1 dV x10.2 y10.0\n"
                                  "n14 v1 dV x10.3 y10.0\n"
                                  "n15 v1 dV x10.4 y10.0\n"
                                  "w20 v1 dV Thighway=primary Nn10,n11,n12\n"
                                  "w21 v1 dV Thighway=secondary Nn12,n13\n"));

    auto conn = db.db().connect();

    CHECK(2 == conn.get_count("osm2pgsql_test_highways"));
    CHECK(0 == conn.get_count("osm2pgsql_test_routes"));

    CHECK(1 == conn.get_count("osm2pgsql_test_highways",
                              "tags->'highway' = 'primary'"));
    CHECK(1 == conn.get_count("osm2pgsql_test_highways",
                              "tags->'highway' = 'secondary'"));
    CHECK(1 == conn.get_count(
                   "osm2pgsql_test_highways",
                   "ST_AsText(geom) = 'LINESTRING(10 10,10 10.2,10.2 10.2)'"));
    CHECK(1 ==
          conn.get_count("osm2pgsql_test_highways",
                         "ST_AsText(geom) = 'LINESTRING(10.2 10.2,10.2 10)'"));

    REQUIRE_NOTHROW(db.run_import(options.append(), "n11 v2 dV x10.0 y10.3\n"));

    CHECK(2 == conn.get_count("osm2pgsql_test_highways"));

    CHECK(1 == conn.get_count("osm2pgsql_test_highways",
                              "tags->'highway' = 'primary'"));
    CHECK(1 == conn.get_count("osm2pgsql_test_highways",
                              "tags->'highway' = 'secondary'"));
    CHECK(1 == conn.get_count(
                   "osm2pgsql_test_highways",
                   "ST_AsText(geom) = 'LINESTRING(10 10,10 10.3,10.2 10.2)'"));
    CHECK(1 ==
          conn.get_count("osm2pgsql_test_highways",
                         "ST_AsText(geom) = 'LINESTRING(10.2 10.2,10.2 10)'"));

    REQUIRE_NOTHROW(db.run_import(
        options.append(),
        "n12 v2 dD\n"
        "w20 v2 dV Thighway=primary Nn10,n11\n"
        "w21 v2 dV Thighway=secondary Nn13\n")); // single node in way!

    CHECK(1 == conn.get_count("osm2pgsql_test_highways"));

    CHECK(1 == conn.get_count("osm2pgsql_test_highways",
                              "tags->'highway' = 'primary'"));
    CHECK(0 == conn.get_count("osm2pgsql_test_highways",
                              "tags->'highway' = 'secondary'"));
    CHECK(1 == conn.get_count("osm2pgsql_test_highways",
                              "ST_AsText(geom) = 'LINESTRING(10 10,10 10.3)'"));

    REQUIRE_NOTHROW(db.run_import(
        options.append(), "w21 v2 dV Thighway=secondary Nn13,n14,n15\n"));

    CHECK(2 == conn.get_count("osm2pgsql_test_highways"));

    CHECK(1 == conn.get_count("osm2pgsql_test_highways",
                              "tags->'highway' = 'primary'"));
    CHECK(1 == conn.get_count("osm2pgsql_test_highways",
                              "tags->'highway' = 'secondary'"));
    CHECK(1 == conn.get_count("osm2pgsql_test_highways",
                              "ST_AsText(geom) = 'LINESTRING(10 10,10 10.3)'"));
    CHECK(1 == conn.get_count(
                   "osm2pgsql_test_highways",
                   "ST_AsText(geom) = 'LINESTRING(10.2 10,10.3 10,10.4 10)'"));
}

TEST_CASE("relation data on ways")
{
    testing::opt_t options = testing::opt_t()
                                 .slim()
                                 .flex("test_output_flex_extra.lua")
                                 .srs(PROJ_LATLONG);

    // create database with three ways and a relation on two of them
    REQUIRE_NOTHROW(
        db.run_import(options, "n10 v1 dV x10.0 y10.0\n"
                               "n11 v1 dV x10.0 y10.2\n"
                               "n12 v1 dV x10.2 y10.2\n"
                               "n13 v1 dV x10.2 y10.0\n"
                               "n14 v1 dV x10.3 y10.0\n"
                               "n15 v1 dV x10.4 y10.0\n"
                               "w20 v1 dV Thighway=primary Nn10,n11,n12\n"
                               "w21 v1 dV Thighway=secondary Nn12,n13\n"
                               "w22 v1 dV Thighway=secondary Nn13,n14,n15\n"
                               "r30 v1 dV Ttype=route,ref=X11 Mw20@,w21@\n"));

    auto conn = db.db().connect();

    CHECK(3 == conn.get_count("osm2pgsql_test_highways"));
    CHECK(1 == conn.get_count("osm2pgsql_test_routes"));

    CHECK(1 == conn.get_count("osm2pgsql_test_highways",
                              "tags->'highway' = 'primary'"));
    CHECK(2 == conn.get_count("osm2pgsql_test_highways",
                              "tags->'highway' = 'secondary'"));

    CHECK(2 == conn.get_count("osm2pgsql_test_highways", "refs = 'X11'"));
    CHECK(1 == conn.get_count("osm2pgsql_test_highways", "refs IS NULL"));

    CHECK(1 == conn.get_count("osm2pgsql_test_routes", "members = '20,21'"));

    // move node in way in the relation
    REQUIRE_NOTHROW(db.run_import(options.append(), "n11 v2 dV x10.0 y10.1\n"));

    CHECK(3 == conn.get_count("osm2pgsql_test_highways"));
    CHECK(1 == conn.get_count("osm2pgsql_test_routes"));

    CHECK(1 == conn.get_count("osm2pgsql_test_highways",
                              "tags->'highway' = 'primary'"));
    CHECK(2 == conn.get_count("osm2pgsql_test_highways",
                              "tags->'highway' = 'secondary'"));

    CHECK(2 == conn.get_count("osm2pgsql_test_highways", "refs = 'X11'"));
    CHECK(1 == conn.get_count("osm2pgsql_test_highways", "refs IS NULL"));

    CHECK(1 == conn.get_count("osm2pgsql_test_routes", "members = '20,21'"));

    // add the third way to the relation
    REQUIRE_NOTHROW(db.run_import(
        options.append(), "r30 v2 dV Ttype=route,ref=X11 Mw20@,w21@,w22@\n"));

    CHECK(3 == conn.get_count("osm2pgsql_test_highways"));
    CHECK(1 == conn.get_count("osm2pgsql_test_routes"));

    CHECK(1 == conn.get_count("osm2pgsql_test_highways",
                              "tags->'highway' = 'primary'"));
    CHECK(2 == conn.get_count("osm2pgsql_test_highways",
                              "tags->'highway' = 'secondary'"));

    CHECK(3 == conn.get_count("osm2pgsql_test_highways", "refs = 'X11'"));
    CHECK(0 == conn.get_count("osm2pgsql_test_highways", "refs IS NULL"));
    CHECK(1 == conn.get_count("osm2pgsql_test_routes", "members = '20,21,22'"));

    // remove the second way from the relation and delete it
    REQUIRE_NOTHROW(db.run_import(
        options.append(), "w21 v2 dD\n"
                          "r30 v3 dV Ttype=route,ref=X11 Mw20@,w22@\n"));

    CHECK(2 == conn.get_count("osm2pgsql_test_highways"));
    CHECK(1 == conn.get_count("osm2pgsql_test_routes"));

    CHECK(1 == conn.get_count("osm2pgsql_test_highways",
                              "tags->'highway' = 'primary'"));
    CHECK(1 == conn.get_count("osm2pgsql_test_highways",
                              "tags->'highway' = 'secondary'"));

    CHECK(2 == conn.get_count("osm2pgsql_test_highways", "refs = 'X11'"));
    CHECK(0 == conn.get_count("osm2pgsql_test_highways", "refs IS NULL"));
    CHECK(1 == conn.get_count("osm2pgsql_test_routes", "members = '20,22'"));

    // delete the relation, leaving two ways
    REQUIRE_NOTHROW(db.run_import(options.append(), "r30 v4 dD\n"));

    CHECK(2 == conn.get_count("osm2pgsql_test_highways"));
    CHECK(0 == conn.get_count("osm2pgsql_test_routes"));

    CHECK(1 == conn.get_count("osm2pgsql_test_highways",
                              "tags->'highway' = 'primary'"));
    CHECK(1 == conn.get_count("osm2pgsql_test_highways",
                              "tags->'highway' = 'secondary'"));

    CHECK(0 == conn.get_count("osm2pgsql_test_highways", "refs = 'X11'"));
    CHECK(2 == conn.get_count("osm2pgsql_test_highways", "refs IS NULL"));
}
