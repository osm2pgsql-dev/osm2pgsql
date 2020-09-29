#include <catch.hpp>

#include "common-import.hpp"
#include "common-options.hpp"

static testing::db::import_t db;

static char const *const conf_file = "test_output_flex_line.lua";

TEST_CASE("linestring in latlon projection (unsplit and split)")
{
    options_t const options = testing::opt_t().flex(conf_file);

    REQUIRE_NOTHROW(db.run_import(options,
                                  "n10 v1 dV x1.0 y1.0\n"
                                  "n11 v1 dV x1.0 y2.0\n"
                                  "n12 v1 dV x1.0 y3.5\n"
                                  "w20 v1 dV Thighway=primary Nn10,n11\n"
                                  "w21 v1 dV Thighway=primary Nn10,n12\n"));

    auto conn = db.db().connect();

    REQUIRE(2 == conn.get_count("osm2pgsql_test_line"));
    REQUIRE(4 == conn.get_count("osm2pgsql_test_split"));
    REQUIRE(4 ==
            conn.get_count("osm2pgsql_test_split", "ST_Length(geom) <= 1.0"));

    REQUIRE(1 == conn.get_count("osm2pgsql_test_split", "way_id=20"));
    REQUIRE(3 == conn.get_count("osm2pgsql_test_split", "way_id=21"));

    REQUIRE(2 == conn.get_count("osm2pgsql_test_split",
                                "way_id=21 AND ST_Length(geom) = 1.0"));

    conn.assert_double(
        1.0, "SELECT ST_Length(geom) FROM osm2pgsql_test_line WHERE way_id=20");
}
