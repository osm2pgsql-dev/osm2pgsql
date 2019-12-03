#include <catch.hpp>

#include "common-import.hpp"
#include "common-options.hpp"

TEST_CASE("multi backend line import")
{
    testing::db::import_t db;

    options_t const options =
        testing::opt_t()
            .slim()
            .multi("test_output_multi_line_trivial.style.json")
            .srs(PROJ_LATLONG);

    REQUIRE_NOTHROW(db.run_file(options, "test_output_multi_line_storage.osm"));

    auto const conn = db.db().connect();
    conn.require_has_table("test_line");

    REQUIRE(3 == conn.get_count("test_line"));

    //check that we have the number of vertexes in each linestring
    REQUIRE(3 ==
            conn.require_scalar<int>(
                "SELECT ST_NumPoints(way) FROM test_line WHERE osm_id = 1"));
    REQUIRE(2 ==
            conn.require_scalar<int>(
                "SELECT ST_NumPoints(way) FROM test_line WHERE osm_id = 2"));
    REQUIRE(2 ==
            conn.require_scalar<int>(
                "SELECT ST_NumPoints(way) FROM test_line WHERE osm_id = 3"));

    REQUIRE(3 == conn.get_count("test_line", "foo = 'bar'"));
}
