#include <catch.hpp>

#include "common-import.hpp"
#include "common-options.hpp"

static testing::db::import_t db;

TEST_CASE("multi backend trivial polygon import")
{
    options_t options = testing::opt_t()
                            .slim()
                            .multi("test_output_multi_poly_trivial.style.json")
                            .srs(PROJ_LATLONG);

    SECTION("Without multi-polygons")
    {
        REQUIRE_NOTHROW(
            db.run_file(options, "test_output_multi_poly_trivial.osm"));

        auto conn = db.db().connect();
        conn.require_has_table("test_poly");

        REQUIRE(2 == conn.get_count("test_poly"));
        REQUIRE(2 == conn.get_count("test_poly", "foo='bar'"));
        REQUIRE(2 == conn.get_count("test_poly", "bar='baz'"));

        // although there are 2 rows, they should both be 5-pointed polygons (note
        // that it's 5 points including the duplicated first/last point)
        REQUIRE(5 == conn.require_scalar<int>(
                         "SELECT DISTINCT ST_NumPoints(ST_ExteriorRing(way)) "
                         "FROM test_poly"));
    }

    SECTION("With multi-polygons")
    {
        options.enable_multi = true;

        REQUIRE_NOTHROW(
            db.run_file(options, "test_output_multi_poly_trivial.osm"));

        auto conn = db.db().connect();
        conn.require_has_table("test_poly");

        REQUIRE(1 == conn.get_count("test_poly"));
        REQUIRE(1 == conn.get_count("test_poly", "foo='bar'"));
        REQUIRE(1 == conn.get_count("test_poly", "bar='baz'"));

        // there should be two 5-pointed polygons in the multipolygon (note that
        // it's 5 points including the duplicated first/last point)
        REQUIRE(2 ==
                conn.get_count(
                    "(SELECT (ST_Dump(way)).geom AS way FROM test_poly) x"));
        REQUIRE(5 ==
                conn.require_scalar<int>(
                    "SELECT DISTINCT ST_NumPoints(ST_ExteriorRing(way)) FROM "
                    "(SELECT (ST_Dump(way)).geom AS way FROM test_poly) x"));
    }
}
