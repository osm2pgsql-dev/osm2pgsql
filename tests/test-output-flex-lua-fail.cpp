#include <catch.hpp>

#include "common-import.hpp"
#include "common-options.hpp"

static testing::db::import_t db;

static char const *const conf_file = "test_output_flex_lua_fail.lua";

TEST_CASE("error in lua file is detected")
{
    options_t const options = testing::opt_t().flex(conf_file);

    REQUIRE_THROWS(db.run_file(options, "liechtenstein-2013-08-03.osm.pbf"));
}
