#include <catch.hpp>

#include "common-import.hpp"
#include "common-options.hpp"

static testing::db::import_t db;

TEST_CASE("non-slim without clustering")
{
    REQUIRE_NOTHROW(db.run_file(
        testing::opt_t().flex("test_output_flex_nocluster.lua"),
        "liechtenstein-2013-08-03.osm.pbf"));

    auto conn = db.db().connect();

    CHECK(1362 == conn.get_count("osm2pgsql_test_point"));
}

TEST_CASE("slim without clustering")
{
    REQUIRE_NOTHROW(db.run_file(
        testing::opt_t().slim().flex("test_output_flex_nocluster.lua"),
        "liechtenstein-2013-08-03.osm.pbf"));

    auto conn = db.db().connect();

    CHECK(1362 == conn.get_count("osm2pgsql_test_point"));
}
