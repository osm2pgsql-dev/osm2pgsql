#include "catch.hpp"

#include "common-import.hpp"
#include "configs.hpp"

static testing::db::import_t db;

TEST_CASE("output_pgsql_t simple import")
{
    SECTION("Regression simple")
    {
        REQUIRE_NOTHROW(db.run_file(testing::options::slim_default(db),
                        "liechtenstein-2013-08-03.osm.pbf"));
    }
}
