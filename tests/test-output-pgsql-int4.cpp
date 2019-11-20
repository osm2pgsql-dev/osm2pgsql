#include <catch.hpp>

#include "common-import.hpp"
#include "common-options.hpp"

static testing::db::import_t db;

TEST_CASE("int4 conversion")
{
    options_t options =
        testing::opt_t().slim().style("test_output_pgsql_int4.style");

    REQUIRE_NOTHROW(db.run_file(options, "test_output_pgsql_int4.osm"));

    auto conn = db.db().connect();

    // First three nodes have population values that are out of range for int4 columns
    conn.assert_null(
        "SELECT population FROM osm2pgsql_test_point WHERE osm_id = 1");
    conn.assert_null(
        "SELECT population FROM osm2pgsql_test_point WHERE osm_id = 2");
    conn.assert_null(
        "SELECT population FROM osm2pgsql_test_point WHERE osm_id = 3");

    // Check values that are valid for int4 columns, including limits
    REQUIRE(
        2147483647 ==
        conn.require_scalar<int>(
            "SELECT population FROM osm2pgsql_test_point WHERE osm_id = 4"));
    REQUIRE(
        10000 ==
        conn.require_scalar<int>(
            "SELECT population FROM osm2pgsql_test_point WHERE osm_id = 5"));
    REQUIRE(
        -10000 ==
        conn.require_scalar<int>(
            "SELECT population FROM osm2pgsql_test_point WHERE osm_id = 6"));
    REQUIRE(
        -2147483648 ==
        conn.require_scalar<int>(
            "SELECT population FROM osm2pgsql_test_point WHERE osm_id = 7"));
    // More out of range negative values
    conn.assert_null(
        "SELECT population FROM osm2pgsql_test_point WHERE osm_id = 8");
    conn.assert_null(
        "SELECT population FROM osm2pgsql_test_point WHERE osm_id = 9");
    conn.assert_null(
        "SELECT population FROM osm2pgsql_test_point WHERE osm_id = 10");

    // Ranges are also parsed into int4 columns
    conn.assert_null(
        "SELECT population FROM osm2pgsql_test_point WHERE osm_id = 11");
    conn.assert_null(
        "SELECT population FROM osm2pgsql_test_point WHERE osm_id = 12");

    // Check values that are valid for int4 columns, including limits
    REQUIRE(
        2147483647 ==
        conn.require_scalar<int>(
            "SELECT population FROM osm2pgsql_test_point WHERE osm_id =13"));
    REQUIRE(
        15000 ==
        conn.require_scalar<int>(
            "SELECT population FROM osm2pgsql_test_point WHERE osm_id = 14"));
    REQUIRE(
        -15000 ==
        conn.require_scalar<int>(
            "SELECT population FROM osm2pgsql_test_point WHERE osm_id = 15"));
    REQUIRE(
        -2147483648 ==
        conn.require_scalar<int>(
            "SELECT population FROM osm2pgsql_test_point WHERE osm_id = 16"));

    // More out of range negative values
    conn.assert_null(
        "SELECT population FROM osm2pgsql_test_point WHERE osm_id = 17");
    conn.assert_null(
        "SELECT population FROM osm2pgsql_test_point WHERE osm_id = 18");
}
