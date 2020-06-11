#include <catch.hpp>

#include "common-import.hpp"
#include "common-options.hpp"

static testing::db::import_t db;

static char const *const conf_file = "test_output_flex_nocluster.lua";
static char const *const data_file = "liechtenstein-2013-08-03.osm.pbf";

TEST_CASE("non-slim without clustering")
{
    REQUIRE_NOTHROW(db.run_file(testing::opt_t().flex(conf_file), data_file));

    auto conn = db.db().connect();

    CHECK(1362 == conn.get_count("osm2pgsql_test_point"));
}

TEST_CASE("slim without clustering")
{
    REQUIRE_NOTHROW(
        db.run_file(testing::opt_t().slim().flex(conf_file), data_file));

    auto conn = db.db().connect();

    CHECK(1362 == conn.get_count("osm2pgsql_test_point"));
}
