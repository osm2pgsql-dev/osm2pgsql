#include <catch.hpp>

#include "common-import.hpp"
#include "common-options.hpp"

static testing::db::import_t db;

static char const *const conf_file = "test_output_flex_validgeom.lua";
static char const *const data_file = "test_output_pgsql_validgeom.osm";

TEST_CASE("no invalid geometries should end up in the database")
{
    options_t const options = testing::opt_t().flex(conf_file);

    REQUIRE_NOTHROW(db.run_file(options, data_file));

    auto conn = db.db().connect();

    REQUIRE(12 == conn.get_count("osm2pgsql_test_polygon"));
    REQUIRE(0 ==
            conn.get_count("osm2pgsql_test_polygon", "NOT ST_IsValid(geom)"));
    REQUIRE(0 == conn.get_count("osm2pgsql_test_polygon", "ST_IsEmpty(geom)"));
}
