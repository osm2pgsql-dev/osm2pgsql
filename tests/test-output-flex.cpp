#include <catch.hpp>

#include "common-import.hpp"
#include "common-options.hpp"

static testing::db::import_t db;

static char const *const conf_file = "test_output_flex.lua";

struct options_slim_default
{
    static options_t options()
    {
        return testing::opt_t().slim().flex(conf_file);
    }
};

struct options_slim_latlon
{
    static options_t options()
    {
        return testing::opt_t().slim().flex(conf_file).srs(PROJ_LATLONG);
    }
};

TEMPLATE_TEST_CASE("liechtenstein regression", "", options_slim_default,
                   options_slim_latlon)
{
    options_t options = TestType::options();

    REQUIRE_NOTHROW(db.run_file(options, "liechtenstein-2013-08-03.osm.pbf"));

    auto conn = db.db().connect();

    conn.require_has_table("osm2pgsql_test_point");
    conn.require_has_table("osm2pgsql_test_line");
    conn.require_has_table("osm2pgsql_test_polygon");
    conn.require_has_table("osm2pgsql_test_route");

    CHECK(1362 == conn.get_count("osm2pgsql_test_point"));
    CHECK(2932 == conn.get_count("osm2pgsql_test_line"));
    CHECK(4136 == conn.get_count("osm2pgsql_test_polygon"));
    CHECK(35 == conn.get_count("osm2pgsql_test_route"));
}
