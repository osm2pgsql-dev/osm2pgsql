#include <catch.hpp>

#include "common-import.hpp"
#include "common-options.hpp"

static testing::db::import_t db;

static char const *const conf_file = "test_output_flex.lua";

TEST_CASE("simple import with tablespaces for middle")
{
    {
        auto conn = db.db().connect();
        REQUIRE(1 ==
                conn.get_count("pg_tablespace", "spcname = 'tablespacetest'"));
    }

    options_t options = testing::opt_t().slim().flex(conf_file);
    options.tblsslim_index = "tablespacetest";
    options.tblsslim_data = "tablespacetest";

    REQUIRE_NOTHROW(db.run_file(options, "liechtenstein-2013-08-03.osm.pbf"));

    auto conn = db.db().connect();

    conn.require_has_table("osm2pgsql_test_point");
    conn.require_has_table("osm2pgsql_test_line");
    conn.require_has_table("osm2pgsql_test_polygon");

    REQUIRE(1362 == conn.get_count("osm2pgsql_test_point"));
    REQUIRE(2932 == conn.get_count("osm2pgsql_test_line"));
    REQUIRE(4136 == conn.get_count("osm2pgsql_test_polygon"));
    REQUIRE(35 == conn.get_count("osm2pgsql_test_route"));
}
