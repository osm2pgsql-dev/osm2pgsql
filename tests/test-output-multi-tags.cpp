#include <catch.hpp>

#include "common-import.hpp"
#include "common-options.hpp"

TEST_CASE("multi backend tag import")
{
    testing::db::import_t db;

    options_t const options = testing::opt_t()
                                  .slim()
                                  .multi("test_output_multi_tags.json")
                                  .srs(PROJ_LATLONG);

    REQUIRE_NOTHROW(db.run_file(options, "test_output_multi_tags.osm"));

    auto const conn = db.db().connect();

    // Check we got the right tables
    conn.require_has_table("test_points_1");
    conn.require_has_table("test_points_2");
    conn.require_has_table("test_line_1");
    conn.require_has_table("test_polygon_1");
    conn.require_has_table("test_polygon_2");

    // Check we didn't get any extra in the tables
    REQUIRE(2 == conn.get_count("test_points_1"));
    REQUIRE(2 == conn.get_count("test_points_2"));
    REQUIRE(1 == conn.get_count("test_line_1"));
    REQUIRE(1 == conn.get_count("test_line_2"));
    REQUIRE(1 == conn.get_count("test_polygon_1"));
    REQUIRE(1 == conn.get_count("test_polygon_2"));

    // Check that the first table for each type got the right transform
    REQUIRE(1 == conn.get_count("test_points_1",
                                "foo IS NULL and bar = 'n1' AND baz IS NULL"));
    REQUIRE(1 == conn.get_count("test_points_1",
                                "foo IS NULL and bar = 'n2' AND baz IS NULL"));
    REQUIRE(1 == conn.get_count("test_line_1",
                                "foo IS NULL and bar = 'w1' AND baz IS NULL"));
    REQUIRE(1 == conn.get_count("test_polygon_1",
                                "foo IS NULL and bar = 'w2' AND baz IS NULL"));

    // Check that the second table also got the right transform
    REQUIRE(1 == conn.get_count("test_points_2",
                                "foo IS NULL and bar IS NULL AND baz = 'n1'"));
    REQUIRE(1 == conn.get_count("test_points_2",
                                "foo IS NULL and bar IS NULL AND baz = 'n2'"));
    REQUIRE(1 == conn.get_count("test_line_2",
                                "foo IS NULL and bar IS NULL AND baz = 'w1'"));
    REQUIRE(1 == conn.get_count("test_polygon_2",
                                "foo IS NULL and bar IS NULL AND baz = 'w2'"));
}
