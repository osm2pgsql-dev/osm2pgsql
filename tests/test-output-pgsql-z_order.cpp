#include <catch.hpp>

#include "common-import.hpp"
#include "common-options.hpp"

static testing::db::import_t db;

TEST_CASE("compute Z order")
{
    REQUIRE_NOTHROW(
        db.run_file(testing::opt_t().slim(), "test_output_pgsql_z_order.osm"));

    auto conn = db.db().connect();

    char const *expected[] = {"motorway", "trunk", "primary", "secondary",
                              "tertiary"};

    for (unsigned i = 0; i < 5; ++i) {
        auto const sql = "SELECT highway FROM osm2pgsql_test_line"
                         " WHERE layer IS NULL ORDER BY z_order DESC"
                         " LIMIT 1 OFFSET {}"_format(i);
        REQUIRE(expected[i] == conn.require_scalar<std::string>(sql));
    }

    REQUIRE("residential" == conn.require_scalar<std::string>(
                                 "SELECT highway FROM osm2pgsql_test_line "
                                 "ORDER BY z_order DESC LIMIT 1 OFFSET 0"));
}
