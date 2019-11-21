#include <catch.hpp>

#include "common-import.hpp"
#include "common-options.hpp"

#include <string>

static testing::db::import_t db;

static std::string population(osmid_t id)
{
    return "SELECT population FROM osm2pgsql_test_point WHERE osm_id = " +
           std::to_string(id);
}

TEST_CASE("int4 conversion")
{
    options_t const options =
        testing::opt_t().slim().style("test_output_pgsql_int4.style");

    REQUIRE_NOTHROW(db.run_file(options, "test_output_pgsql_int4.osm"));

    auto conn = db.db().connect();

    // First three nodes have population values that are out of range for int4 columns
    conn.assert_null(population(1));
    conn.assert_null(population(2));
    conn.assert_null(population(3));

    // Check values that are valid for int4 columns, including limits
    CHECK(2147483647 == conn.require_scalar<int>(population(4)));
    CHECK(10000 == conn.require_scalar<int>(population(5)));
    CHECK(-10000 == conn.require_scalar<int>(population(6)));
    CHECK(-2147483648 == conn.require_scalar<int>(population(7)));

    // More out of range negative values
    conn.assert_null(population(8));
    conn.assert_null(population(9));
    conn.assert_null(population(10));

    // Ranges are also parsed into int4 columns
    conn.assert_null(population(11));
    conn.assert_null(population(12));

    // Check values that are valid for int4 columns, including limits
    CHECK(2147483647 == conn.require_scalar<int>(population(13)));
    CHECK(15000 == conn.require_scalar<int>(population(14)));
    CHECK(-15000 == conn.require_scalar<int>(population(15)));
    CHECK(-2147483648 == conn.require_scalar<int>(population(16)));

    // More out of range negative values
    conn.assert_null(population(17));
    conn.assert_null(population(18));

    // More invalid values
    conn.assert_null(population(19));
    conn.assert_null(population(20));
    conn.assert_null(population(21));
    conn.assert_null(population(22));

    // Zero is a valid value
    CHECK(0 == conn.require_scalar<int>(population(23)));
}
