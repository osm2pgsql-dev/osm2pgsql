#include <catch.hpp>

#include "common-import.hpp"
#include "common-options.hpp"

static testing::db::import_t db;

static char const *const conf_file = "test_output_flex_stage2.lua";

TEST_CASE("nodes and ways")
{
    options_t options = testing::opt_t().slim().flex(conf_file);

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

    options.append = true;

    REQUIRE_NOTHROW(db.run_import(options, "n11 v2 dV x10.0 y10.3\n"));

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
        options,
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

    REQUIRE_NOTHROW(
        db.run_import(options, "w21 v2 dV Thighway=secondary Nn13,n14,n15\n"));

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
    options_t options = testing::opt_t().slim().flex(conf_file);

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

    options.append = true;

    // move node in way in the relation
    REQUIRE_NOTHROW(db.run_import(options, "n11 v2 dV x10.0 y10.1\n"));

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
        options, "r30 v2 dV Ttype=route,ref=X11 Mw20@,w21@,w22@\n"));

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
    REQUIRE_NOTHROW(
        db.run_import(options, "w21 v2 dD\n"
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
    REQUIRE_NOTHROW(db.run_import(options, "r30 v4 dD\n"));

    CHECK(2 == conn.get_count("osm2pgsql_test_highways"));
    CHECK(0 == conn.get_count("osm2pgsql_test_routes"));

    CHECK(1 == conn.get_count("osm2pgsql_test_highways",
                              "tags->'highway' = 'primary'"));
    CHECK(1 == conn.get_count("osm2pgsql_test_highways",
                              "tags->'highway' = 'secondary'"));

    CHECK(0 == conn.get_count("osm2pgsql_test_highways", "refs = 'X11'"));
    CHECK(2 == conn.get_count("osm2pgsql_test_highways", "refs IS NULL"));
}

TEST_CASE("relation data on ways: delete or re-tag relation")
{
    options_t options = testing::opt_t().slim().flex(conf_file);

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

    options.append = true;

    SECTION("delete relation")
    {
        REQUIRE_NOTHROW(db.run_import(options, "r30 v2 dD\n"));
    }

    SECTION("change tags on relation")
    {
        REQUIRE_NOTHROW(
            db.run_import(options, "r30 v2 dV Ttype=foo Mw20@,w21@\n"));
    }

    CHECK(3 == conn.get_count("osm2pgsql_test_highways"));
    CHECK(0 == conn.get_count("osm2pgsql_test_routes"));

    CHECK(1 == conn.get_count("osm2pgsql_test_highways",
                              "tags->'highway' = 'primary'"));
    CHECK(2 == conn.get_count("osm2pgsql_test_highways",
                              "tags->'highway' = 'secondary'"));

    CHECK(0 == conn.get_count("osm2pgsql_test_highways", "refs = 'X11'"));
    CHECK(3 == conn.get_count("osm2pgsql_test_highways", "refs IS NULL"));

    CHECK(0 == conn.get_count("osm2pgsql_test_routes", "members = '20,21'"));
}

TEST_CASE("relation data on ways: delete way in other relation")
{
    options_t options = testing::opt_t().slim().flex(conf_file);

    // create database with three ways and two relations on them
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
                               "r30 v1 dV Ttype=no-route Mw20@,w21@\n"
                               "r31 v1 dV Ttype=route,ref=X11 Mw21@,w22@\n"));

    auto conn = db.db().connect();

    CHECK(3 == conn.get_count("osm2pgsql_test_highways"));
    CHECK(1 == conn.get_count("osm2pgsql_test_routes"));

    CHECK(1 == conn.get_count("osm2pgsql_test_highways",
                              "tags->'highway' = 'primary'"));
    CHECK(2 == conn.get_count("osm2pgsql_test_highways",
                              "tags->'highway' = 'secondary'"));

    CHECK(2 == conn.get_count("osm2pgsql_test_highways", "refs = 'X11'"));
    CHECK(1 == conn.get_count("osm2pgsql_test_highways", "refs IS NULL"));

    CHECK(0 == conn.get_count("osm2pgsql_test_routes", "members = '20,21'"));
    CHECK(1 == conn.get_count("osm2pgsql_test_routes", "members = '21,22'"));

    options.append = true;

    SECTION("change way node list")
    {
        REQUIRE_NOTHROW(
            db.run_import(options, "w20 v2 dV Thighway=primary Nn10,n11\n"));
    }

    SECTION("change way tags")
    {
        REQUIRE_NOTHROW(db.run_import(
            options, "w20 v2 dV Thighway=primary,name=foo Nn10,n11,n12\n"));
    }

    SECTION("change way node")
    {
        REQUIRE_NOTHROW(db.run_import(options, "n10 v2 dV x11.0 y10.0\n"));
    }

    CHECK(3 == conn.get_count("osm2pgsql_test_highways"));
    CHECK(1 == conn.get_count("osm2pgsql_test_routes"));

    CHECK(1 == conn.get_count("osm2pgsql_test_highways",
                              "tags->'highway' = 'primary'"));
    CHECK(2 == conn.get_count("osm2pgsql_test_highways",
                              "tags->'highway' = 'secondary'"));

    CHECK(2 == conn.get_count("osm2pgsql_test_highways", "refs = 'X11'"));
    CHECK(1 == conn.get_count("osm2pgsql_test_highways", "refs IS NULL"));

    CHECK(0 == conn.get_count("osm2pgsql_test_routes", "members = '20,21'"));
    CHECK(1 == conn.get_count("osm2pgsql_test_routes", "members = '21,22'"));
}

TEST_CASE("relation data on ways: changing things in one relation should not "
          "change output")
{
    options_t options = testing::opt_t().slim().flex(conf_file);

    // create database with three ways and two relations on them
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
                               "r30 v1 dV Ttype=route,ref=Y11 Mw20@,w21@\n"
                               "r31 v1 dV Ttype=route,ref=X11 Mw21@,w22@\n"));

    auto conn = db.db().connect();

    CHECK(3 == conn.get_count("osm2pgsql_test_highways"));
    CHECK(2 == conn.get_count("osm2pgsql_test_routes"));

    CHECK(1 == conn.get_count("osm2pgsql_test_highways",
                              "tags->'highway' = 'primary'"));
    CHECK(2 == conn.get_count("osm2pgsql_test_highways",
                              "tags->'highway' = 'secondary'"));

    CHECK(1 == conn.get_count("osm2pgsql_test_highways", "refs = 'X11'"));
    CHECK(1 == conn.get_count("osm2pgsql_test_highways", "refs = 'Y11'"));
    CHECK(1 == conn.get_count("osm2pgsql_test_highways", "refs = 'X11,Y11'"));

    CHECK(1 == conn.get_count("osm2pgsql_test_routes", "members = '20,21'"));
    CHECK(1 == conn.get_count("osm2pgsql_test_routes", "members = '21,22'"));

    options.append = true;

    SECTION("new version of relation")
    {
        REQUIRE_NOTHROW(db.run_import(
            options, "r30 v2 dV Ttype=route,ref=Y11 Mw20@,w21@\n"));
    }

    SECTION("change way node list")
    {
        REQUIRE_NOTHROW(
            db.run_import(options, "w20 v2 dV Thighway=primary Nn10,n11\n"));
    }

    SECTION("change way tags")
    {
        REQUIRE_NOTHROW(db.run_import(
            options, "w20 v2 dV Thighway=primary,name=foo Nn10,n11,n12\n"));
    }

    SECTION("change way node")
    {
        REQUIRE_NOTHROW(db.run_import(options, "n10 v2 dV x11.0 y10.0\n"));
    }

    CHECK(3 == conn.get_count("osm2pgsql_test_highways"));
    CHECK(2 == conn.get_count("osm2pgsql_test_routes"));

    CHECK(1 == conn.get_count("osm2pgsql_test_highways",
                              "tags->'highway' = 'primary'"));
    CHECK(2 == conn.get_count("osm2pgsql_test_highways",
                              "tags->'highway' = 'secondary'"));

    CHECK(1 == conn.get_count("osm2pgsql_test_highways", "refs = 'X11'"));
    CHECK(1 == conn.get_count("osm2pgsql_test_highways", "refs = 'Y11'"));
    CHECK(1 == conn.get_count("osm2pgsql_test_highways", "refs = 'X11,Y11'"));

    CHECK(1 == conn.get_count("osm2pgsql_test_routes", "members = '20,21'"));
    CHECK(1 == conn.get_count("osm2pgsql_test_routes", "members = '21,22'"));
}

TEST_CASE("relation data on ways: change relation (two rels)")
{
    options_t options = testing::opt_t().slim().flex(conf_file);

    // create database with three ways and two relations on them
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
                               "r30 v1 dV Ttype=route,ref=Y11 Mw20@,w21@\n"
                               "r31 v1 dV Ttype=route,ref=X11 Mw21@,w22@\n"));

    auto conn = db.db().connect();

    CHECK(3 == conn.get_count("osm2pgsql_test_highways"));
    CHECK(2 == conn.get_count("osm2pgsql_test_routes"));

    CHECK(1 == conn.get_count("osm2pgsql_test_highways",
                              "tags->'highway' = 'primary'"));
    CHECK(2 == conn.get_count("osm2pgsql_test_highways",
                              "tags->'highway' = 'secondary'"));

    CHECK(1 == conn.get_count("osm2pgsql_test_highways", "refs = 'X11'"));
    CHECK(1 == conn.get_count("osm2pgsql_test_highways", "refs = 'Y11'"));
    CHECK(1 == conn.get_count("osm2pgsql_test_highways", "refs = 'X11,Y11'"));

    CHECK(1 == conn.get_count("osm2pgsql_test_routes", "members = '20,21'"));
    CHECK(1 == conn.get_count("osm2pgsql_test_routes", "members = '21,22'"));

    options.append = true;

    REQUIRE_NOTHROW(
        db.run_import(options, "r30 v2 dV Ttype=route,ref=Z11 Mw20@,w21@\n"));

    CHECK(3 == conn.get_count("osm2pgsql_test_highways"));
    CHECK(2 == conn.get_count("osm2pgsql_test_routes"));

    CHECK(1 == conn.get_count("osm2pgsql_test_highways",
                              "tags->'highway' = 'primary'"));
    CHECK(2 == conn.get_count("osm2pgsql_test_highways",
                              "tags->'highway' = 'secondary'"));

    CHECK(1 == conn.get_count("osm2pgsql_test_highways", "refs = 'X11'"));
    CHECK(1 == conn.get_count("osm2pgsql_test_highways", "refs = 'Z11'"));
    CHECK(1 == conn.get_count("osm2pgsql_test_highways", "refs = 'X11,Z11'"));

    CHECK(1 == conn.get_count("osm2pgsql_test_routes", "members = '20,21'"));
    CHECK(1 == conn.get_count("osm2pgsql_test_routes", "members = '21,22'"));
}

TEST_CASE("relation data on ways: change relation (three rels)")
{
    options_t options = testing::opt_t().slim().flex(conf_file);

    // create database with three ways and two relations on them
    REQUIRE_NOTHROW(db.run_import(options,
                                  "n10 v1 dV x10.0 y10.0\n"
                                  "n11 v1 dV x10.0 y10.2\n"
                                  "n12 v1 dV x10.2 y10.2\n"
                                  "n13 v1 dV x10.2 y10.0\n"
                                  "n14 v1 dV x10.3 y10.0\n"
                                  "n15 v1 dV x10.4 y10.0\n"
                                  "w20 v1 dV Thighway=primary Nn10,n11,n12\n"
                                  "w21 v1 dV Thighway=secondary Nn12,n13\n"
                                  "w22 v1 dV Thighway=secondary Nn13,n14,n15\n"
                                  "r30 v1 dV Ttype=route,ref=Y11 Mw20@,w21@\n"
                                  "r31 v1 dV Ttype=route,ref=X11 Mw21@,w22@\n"
                                  "r32 v1 dV Ttype=route,ref=Z11 Mw22@\n"));

    auto conn = db.db().connect();

    CHECK(3 == conn.get_count("osm2pgsql_test_highways"));
    CHECK(3 == conn.get_count("osm2pgsql_test_routes"));

    CHECK(1 == conn.get_count("osm2pgsql_test_highways",
                              "tags->'highway' = 'primary'"));
    CHECK(2 == conn.get_count("osm2pgsql_test_highways",
                              "tags->'highway' = 'secondary'"));

    CHECK(1 == conn.get_count("osm2pgsql_test_highways", "refs = 'Y11'"));
    CHECK(1 == conn.get_count("osm2pgsql_test_highways", "refs = 'X11,Y11'"));
    CHECK(1 == conn.get_count("osm2pgsql_test_highways", "refs = 'X11,Z11'"));

    CHECK(1 == conn.get_count("osm2pgsql_test_routes", "members = '20,21'"));
    CHECK(1 == conn.get_count("osm2pgsql_test_routes", "members = '21,22'"));
    CHECK(1 == conn.get_count("osm2pgsql_test_routes", "members = '22'"));

    options.append = true;

    SECTION("change way node list")
    {
        REQUIRE_NOTHROW(
            db.run_import(options, "w20 v2 dV Thighway=primary Nn10,n11\n"));
    }

    SECTION("change way tags")
    {
        REQUIRE_NOTHROW(db.run_import(
            options, "w20 v2 dV Thighway=primary,name=foo Nn10,n11,n12\n"));
    }

    SECTION("change way node")
    {
        REQUIRE_NOTHROW(db.run_import(options, "n10 v2 dV x11.0 y10.0\n"));
    }

    CHECK(3 == conn.get_count("osm2pgsql_test_highways"));
    CHECK(3 == conn.get_count("osm2pgsql_test_routes"));

    CHECK(1 == conn.get_count("osm2pgsql_test_highways",
                              "tags->'highway' = 'primary'"));
    CHECK(2 == conn.get_count("osm2pgsql_test_highways",
                              "tags->'highway' = 'secondary'"));

    CHECK(1 == conn.get_count("osm2pgsql_test_highways", "refs = 'Y11'"));
    CHECK(1 == conn.get_count("osm2pgsql_test_highways", "refs = 'X11,Y11'"));
    CHECK(1 == conn.get_count("osm2pgsql_test_highways", "refs = 'X11,Z11'"));
    CHECK(0 == conn.get_count("osm2pgsql_test_highways", "refs = 'X11'"));

    CHECK(1 == conn.get_count("osm2pgsql_test_routes", "members = '20,21'"));
    CHECK(1 == conn.get_count("osm2pgsql_test_routes", "members = '21,22'"));
    CHECK(1 == conn.get_count("osm2pgsql_test_routes", "members = '22'"));
}

TEST_CASE("relation data on ways: delete relation")
{
    options_t options =
        testing::opt_t().slim().flex("test_output_flex_stage2_alt.lua");

    // create database with a way and two relations on it
    REQUIRE_NOTHROW(db.run_import(options,
                                  "n10 v1 dV x10.0 y10.0\n"
                                  "n11 v1 dV x10.0 y10.2\n"
                                  "n12 v1 dV x10.2 y10.2\n"
                                  "w20 v1 dV Thighway=primary Nn10,n11,n12\n"
                                  "r30 v1 dV Ttype=route,ref=Y11 Mw20@\n"
                                  "r31 v1 dV Ttype=something Mw20@\n"));

    auto conn = db.db().connect();

    CHECK(1 == conn.get_count("osm2pgsql_test_highways"));
    CHECK(1 == conn.get_count("osm2pgsql_test_routes"));
    CHECK(1 == conn.get_count("osm2pgsql_test_highways", "refs = 'Y11'"));

    options.append = true;

    // delete the non-route relation
    REQUIRE_NOTHROW(db.run_import(options, "r31 v2 dD\n"));

    CHECK(1 == conn.get_count("osm2pgsql_test_highways"));
    CHECK(1 == conn.get_count("osm2pgsql_test_routes"));
    CHECK(1 == conn.get_count("osm2pgsql_test_highways", "refs = 'Y11'"));
}
