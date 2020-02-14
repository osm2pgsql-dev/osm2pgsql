#include <catch.hpp>

#include "common-import.hpp"
#include "common-options.hpp"

static testing::db::import_t db;

TEST_CASE("invalid way geometry should be ignored")
{
    testing::opt_t options =
        testing::opt_t().slim().flex("test_output_flex.lua").srs(PROJ_LATLONG);

    REQUIRE_NOTHROW(
        db.run_import(options,
                      "n10 v1 dV x10.0 y10.0\n"
                      "n11 v1 dV x10.0 y10.2\n"
                      "n12 v1 dV x10.2 y10.2\n"
                      "w20 v1 dV Thighway=primary Nn10,n12\n"    // okay
                      "w21 v1 dV Thighway=primary Nn10,n13\n")); // not okay
    auto conn = db.db().connect();

    CHECK(0 == conn.get_count("osm2pgsql_test_point"));
    CHECK(1 == conn.get_count("osm2pgsql_test_line"));
    CHECK(0 == conn.get_count("osm2pgsql_test_polygon"));
    CHECK(0 == conn.get_count("osm2pgsql_test_route"));
}

TEST_CASE("invalid area geometry from way should be ignored")
{
    testing::opt_t options =
        testing::opt_t().slim().flex("test_output_flex.lua").srs(PROJ_LATLONG);

    REQUIRE_NOTHROW(db.run_import(
        options,
        "n10 v1 dV x10.0 y10.0\n"
        "n11 v1 dV x10.0 y10.2\n"
        "n12 v1 dV x10.2 y10.2\n"
        "w20 v1 dV Tnatural=wood Nn10,n11,n12,n10\n"     // okay
        "w21 v1 dV Tnatural=wood Nn10,n11,n12,n13,n10\n" // okay
        "w22 v1 dV Tnatural=wood Nn10,n11,n12,n10,n11\n" // not okay
        "w23 v1 dV Tnatural=wood Nn10,n11,n12\n"));      // not okay

    auto conn = db.db().connect();

    CHECK(0 == conn.get_count("osm2pgsql_test_point"));
    CHECK(0 == conn.get_count("osm2pgsql_test_line"));
    CHECK(2 == conn.get_count("osm2pgsql_test_polygon"));
    CHECK(0 == conn.get_count("osm2pgsql_test_route"));
}

TEST_CASE("invalid area geometry from way should be ignored 2")
{
    testing::opt_t options =
        testing::opt_t().slim().flex("test_output_flex.lua").srs(PROJ_LATLONG);

    REQUIRE_NOTHROW(db.run_import(
        options, "n10 v1 dV x1.70 y1.78\n"
                 "n11 v1 dV x1.87 y1.68\n"
                 "n12 v1 dV x1.84 y1.84\n"
                 "n13 v1 dV x1.82 y1.67\n"
                 "w20 v1 dV Tamenity=parking Nn10,n11,n12,n13,n10\n"));

    auto conn = db.db().connect();

    CHECK(0 == conn.get_count("osm2pgsql_test_point"));
    CHECK(0 == conn.get_count("osm2pgsql_test_line"));
    CHECK(0 == conn.get_count("osm2pgsql_test_polygon"));
    CHECK(0 == conn.get_count("osm2pgsql_test_route"));
}

TEST_CASE("invalid area geometry from relation should be ignored")
{
    testing::opt_t options =
        testing::opt_t().slim().flex("test_output_flex.lua").srs(PROJ_LATLONG);

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
                                  "Mw20@\n" // not okay
                                  "r32 v1 dV Ttype=multipolygon,landuse=forest "
                                  "Mw20@,w22@\n")); // not okay

    auto conn = db.db().connect();

    CHECK(0 == conn.get_count("osm2pgsql_test_point"));
    CHECK(0 == conn.get_count("osm2pgsql_test_line"));
    CHECK(1 == conn.get_count("osm2pgsql_test_polygon"));
    CHECK(0 == conn.get_count("osm2pgsql_test_route"));
}
