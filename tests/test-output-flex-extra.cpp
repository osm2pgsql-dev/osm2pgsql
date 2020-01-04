#include <catch.hpp>

#include "common-import.hpp"
#include "common-options.hpp"

static testing::db::import_t db;

TEST_CASE("relation data on ways")
{
    // create database with three ways and a relation on two of them
    REQUIRE_NOTHROW(db.run_import(
        testing::opt_t().slim().flex("test_output_flex_extra.lua"),
        "n10 v1 dV x10.0 y10.0\n"
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

    CHECK(1 == conn.get_count(
                   "osm2pgsql_test_highways",
                   "abs(min_x - 10.0) < 0.01 AND abs(min_y - 10.0) < 0.01 AND "
                   "abs(max_x - 10.2) < 0.01 AND abs(max_y - 10.2) < 0.01"));

    CHECK(1 == conn.get_count("osm2pgsql_test_routes", "members = '20,21'"));

    // add the third way to the relation
    REQUIRE_NOTHROW(db.run_import(
        testing::opt_t().slim().append().flex("test_output_flex_extra.lua"),
        "r30 v2 dV Ttype=route,ref=X11 Mw20@,w21@,w22@\n"));

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
        testing::opt_t().slim().append().flex("test_output_flex_extra.lua"),
        "w21 v2 dD\n"
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
    REQUIRE_NOTHROW(db.run_import(
        testing::opt_t().slim().append().flex("test_output_flex_extra.lua"),
        "r30 v4 dD\n"));

    CHECK(2 == conn.get_count("osm2pgsql_test_highways"));
    CHECK(0 == conn.get_count("osm2pgsql_test_routes"));

    CHECK(1 == conn.get_count("osm2pgsql_test_highways",
                              "tags->'highway' = 'primary'"));
    CHECK(1 == conn.get_count("osm2pgsql_test_highways",
                              "tags->'highway' = 'secondary'"));

    CHECK(0 == conn.get_count("osm2pgsql_test_highways", "refs = 'X11'"));
    CHECK(2 == conn.get_count("osm2pgsql_test_highways", "refs IS NULL"));
}
