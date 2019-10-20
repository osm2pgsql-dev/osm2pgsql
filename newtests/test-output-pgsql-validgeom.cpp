#include "catch.hpp"

#include "common-import.hpp"
#include "configs.hpp"

static testing::db::import_t db;

TEST_CASE("no invalid geometries")
{
    testing::options::slim_default options(db.db());

    REQUIRE_NOTHROW(db.run_file(options, "test_output_pgsql_validgeom.osm"));

    auto conn = db.db().connect();

    conn.require_has_table("osm2pgsql_test_point");
    conn.require_has_table("osm2pgsql_test_line");
    conn.require_has_table("osm2pgsql_test_polygon");
    conn.require_has_table("osm2pgsql_test_roads");

    REQUIRE(12 == conn.get_count("osm2pgsql_test_polygon"));
    REQUIRE(0 == conn.get_count("osm2pgsql_test_polygon", "NOT ST_IsValid(way)"));
    REQUIRE(0 == conn.get_count("osm2pgsql_test_polygon", "ST_IsEmpty(way)"));
}
