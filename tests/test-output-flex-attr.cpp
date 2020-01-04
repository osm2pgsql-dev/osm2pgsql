#include <catch.hpp>

#include "common-import.hpp"
#include "common-options.hpp"

static testing::db::import_t db;

TEST_CASE("no extra_attributes")
{
    REQUIRE_NOTHROW(
        db.run_import(testing::opt_t().slim().flex("test_output_flex_attr.lua"),
                      "n10 v1 dV x10.0 y10.0\n"
                      "n11 v1 dV x10.0 y10.2\n"
                      "n12 v1 dV x10.2 y10.2\n"
                      "w20 v1 dV c31 t2020-01-12T12:34:56Z i17 utest "
                      "Thighway=primary Nn10,n11,n12\n"));

    auto conn = db.db().connect();

    CHECK(1 == conn.get_count("osm2pgsql_test_ways_attr"));
    CHECK(1 == conn.get_count("osm2pgsql_test_ways_attr",
                              "tags->'highway' = 'primary'"));
    CHECK(0 == conn.get_count("osm2pgsql_test_ways_attr", "version = 1"));
    CHECK(0 == conn.get_count("osm2pgsql_test_ways_attr", "changeset = 31"));
    CHECK(0 ==
          conn.get_count("osm2pgsql_test_ways_attr", "timestamp = 1578832496"));
    CHECK(0 == conn.get_count("osm2pgsql_test_ways_attr", "uid = 17"));
    CHECK(0 == conn.get_count("osm2pgsql_test_ways_attr", "\"user\" = 'test'"));

    REQUIRE_NOTHROW(db.run_import(
        testing::opt_t().slim().append().flex("test_output_flex_attr.lua"),
        "n10 v2 dV x11.0 y11.0\n"));

    CHECK(1 == conn.get_count("osm2pgsql_test_ways_attr"));
    CHECK(1 == conn.get_count("osm2pgsql_test_ways_attr",
                              "tags->'highway' = 'primary'"));
    CHECK(0 == conn.get_count("osm2pgsql_test_ways_attr", "version = 1"));
    CHECK(0 == conn.get_count("osm2pgsql_test_ways_attr", "changeset = 31"));
    CHECK(0 ==
          conn.get_count("osm2pgsql_test_ways_attr", "timestamp = 1578832496"));
    CHECK(0 == conn.get_count("osm2pgsql_test_ways_attr", "uid = 17"));
    CHECK(0 == conn.get_count("osm2pgsql_test_ways_attr", "\"user\" = 'test'"));
}

TEST_CASE("with extra_attributes")
{
    REQUIRE_NOTHROW(
        db.run_import(testing::opt_t().extra_attributes().slim().flex(
                          "test_output_flex_attr.lua"),
                      "n10 v1 dV x10.0 y10.0\n"
                      "n11 v1 dV x10.0 y10.2\n"
                      "n12 v1 dV x10.2 y10.2\n"
                      "w20 v1 dV c31 t2020-01-12T12:34:56Z i17 utest "
                      "Thighway=primary Nn10,n11,n12\n"));

    auto conn = db.db().connect();

    CHECK(1 == conn.get_count("osm2pgsql_test_ways_attr"));
    CHECK(1 == conn.get_count("osm2pgsql_test_ways_attr",
                              "tags->'highway' = 'primary'"));
    CHECK(1 == conn.get_count("osm2pgsql_test_ways_attr", "version = 1"));
    CHECK(1 == conn.get_count("osm2pgsql_test_ways_attr", "changeset = 31"));
    CHECK(1 ==
          conn.get_count("osm2pgsql_test_ways_attr", "timestamp = 1578832496"));
    CHECK(1 == conn.get_count("osm2pgsql_test_ways_attr", "uid = 17"));
    CHECK(1 == conn.get_count("osm2pgsql_test_ways_attr", "\"user\" = 'test'"));

    REQUIRE_NOTHROW(
        db.run_import(testing::opt_t().extra_attributes().slim().append().flex(
                          "test_output_flex_attr.lua"),
                      "n10 v2 dV x11.0 y11.0\n"));

    CHECK(1 == conn.get_count("osm2pgsql_test_ways_attr"));
    CHECK(1 == conn.get_count("osm2pgsql_test_ways_attr",
                              "tags->'highway' = 'primary'"));
    CHECK(1 == conn.get_count("osm2pgsql_test_ways_attr", "version = 1"));
    CHECK(1 == conn.get_count("osm2pgsql_test_ways_attr", "changeset = 31"));
    CHECK(1 ==
          conn.get_count("osm2pgsql_test_ways_attr", "timestamp = 1578832496"));
    CHECK(1 == conn.get_count("osm2pgsql_test_ways_attr", "uid = 17"));
    CHECK(1 == conn.get_count("osm2pgsql_test_ways_attr", "\"user\" = 'test'"));
}
