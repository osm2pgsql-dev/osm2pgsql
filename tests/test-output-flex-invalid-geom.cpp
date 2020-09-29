#include <catch.hpp>

#include "common-import.hpp"
#include "common-options.hpp"

static testing::db::import_t db;

static char const *const conf_file = "test_output_flex_invalid_geom.lua";

TEST_CASE("invalid way geometry should be ignored")
{
    options_t const options = testing::opt_t().flex(conf_file);

    REQUIRE_NOTHROW(db.run_import(
        options,
        "n10 v1 dV x10.0 y10.0\n"
        "n11 v1 dV x10.0 y10.2\n"
        "n12 v1 dV x10.2 y10.2\n"
        "n14 v1 dV x10.0 y10.0\n"
        "w20 v1 dV Thighway=primary Nn10,n12\n"     // okay
        "w21 v1 dV Thighway=primary Nn10,n12,n13\n" // okay, unknown node ignored
        "w22 v1 dV Thighway=primary Nn10,n13\n" // not okay, unknown node leads to single-node line
        "w23 v1 dV Thighway=primary Nn10\n"     // not okay, single node in way
        "w24 v1 dV Thighway=primary Nn10,n10\n" // not okay, same node twice
        "w25 v1 dV Thighway=primary Nn10,n14\n")); // not okay, same location twice

    auto conn = db.db().connect();

    CHECK(2 == conn.get_count("osm2pgsql_test_line"));
    CHECK(0 == conn.get_count("osm2pgsql_test_polygon"));
}

TEST_CASE("invalid area geometry from way should be ignored")
{
    options_t const options = testing::opt_t().flex(conf_file);

    REQUIRE_NOTHROW(db.run_import(
        options,
        "n10 v1 dV x10.0 y10.0\n"
        "n11 v1 dV x10.0 y10.2\n"
        "n12 v1 dV x10.2 y10.2\n"
        "w20 v1 dV Tnatural=wood Nn10,n11,n12,n10\n" // okay
        "w21 v1 dV Tnatural=wood Nn10,n11,n12,n13,n10\n" // okay, unknown node ignored
        "w22 v1 dV Tnatural=wood Nn10,n11,n12,n10,n11\n" // not okay, duplicate segment
        "w23 v1 dV Tnatural=wood Nn10,n11,n12\n")); // not okay, ring not closed

    auto conn = db.db().connect();

    CHECK(0 == conn.get_count("osm2pgsql_test_line"));
    CHECK(2 == conn.get_count("osm2pgsql_test_polygon"));
}

TEST_CASE("area with self-intersection from way should be ignored")
{
    options_t const options = testing::opt_t().flex(conf_file);

    REQUIRE_NOTHROW(db.run_import(
        options, "n10 v1 dV x1.70 y1.78\n"
                 "n11 v1 dV x1.87 y1.68\n"
                 "n12 v1 dV x1.84 y1.84\n"
                 "n13 v1 dV x1.82 y1.67\n"
                 "w20 v1 dV Tnatural=wood Nn10,n11,n12,n13,n10\n"));

    auto conn = db.db().connect();

    CHECK(0 == conn.get_count("osm2pgsql_test_line"));
    CHECK(0 == conn.get_count("osm2pgsql_test_polygon"));
}

TEST_CASE("invalid area geometry from relation should be ignored")
{
    options_t const options = testing::opt_t().flex(conf_file);

    REQUIRE_NOTHROW(db.run_import(options,
                                  "n10 v1 dV x10.0 y10.0\n"
                                  "n11 v1 dV x10.0 y10.2\n"
                                  "n12 v1 dV x10.2 y10.2\n"
                                  "n13 v1 dV x10.2 y10.0\n"
                                  "w20 v1 dV Nn10,n11,n12\n"
                                  "w21 v1 dV Nn12,n13,n10\n"
                                  "r30 v1 dV Ttype=multipolygon,landuse=forest "
                                  "Mw20@,w21@\n" // okay
                                  "r31 v1 dV Ttype=multipolygon,landuse=forest "
                                  "Mw20@\n" // not okay, ring not closed
                                  "r32 v1 dV Ttype=multipolygon,landuse=forest "
                                  "Mw20@,w22@\n")); // not okay, missing way

    auto conn = db.db().connect();

    CHECK(0 == conn.get_count("osm2pgsql_test_line"));
    CHECK(1 == conn.get_count("osm2pgsql_test_polygon"));
}
