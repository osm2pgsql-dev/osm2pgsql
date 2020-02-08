#include <catch.hpp>

#include "common-import.hpp"
#include "common-options.hpp"

static testing::db::import_t db;

TEST_CASE("error in lua file")
{
    REQUIRE_THROWS(
        db.run_file(testing::opt_t().slim().flex("test_output_flex_fail.lua"),
                    "liechtenstein-2013-08-03.osm.pbf"));
}

