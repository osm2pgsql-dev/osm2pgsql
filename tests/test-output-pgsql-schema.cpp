#include <catch.hpp>

#include "common-import.hpp"
#include "common-options.hpp"

static testing::db::import_t db;

TEST_CASE("schema separation")
{
    {
        auto conn = db.db().connect();
        conn.exec("CREATE SCHEMA myschema;"
                  "CREATE TABLE myschema.osm2pgsql_test_point (id bigint);"
                  "CREATE TABLE myschema.osm2pgsql_test_line (id bigint);"
                  "CREATE TABLE myschema.osm2pgsql_test_polygon (id bigint);"
                  "CREATE TABLE myschema.osm2pgsql_test_roads (id bigint)");
    }

    REQUIRE_NOTHROW(
        db.run_file(testing::opt_t().slim(), "test_output_pgsql_z_order.osm"));

    auto conn = db.db().connect();

    conn.require_has_table("public.osm2pgsql_test_point");
    conn.require_has_table("public.osm2pgsql_test_line");
    conn.require_has_table("public.osm2pgsql_test_polygon");
    conn.require_has_table("public.osm2pgsql_test_roads");
    conn.require_has_table("myschema.osm2pgsql_test_point");
    conn.require_has_table("myschema.osm2pgsql_test_line");
    conn.require_has_table("myschema.osm2pgsql_test_polygon");
    conn.require_has_table("myschema.osm2pgsql_test_roads");

    REQUIRE(2 == conn.get_count("osm2pgsql_test_point"));
    REQUIRE(11 == conn.get_count("osm2pgsql_test_line"));
    REQUIRE(1 == conn.get_count("osm2pgsql_test_polygon"));
    REQUIRE(8 == conn.get_count("osm2pgsql_test_roads"));
    REQUIRE(0 == conn.get_count("myschema.osm2pgsql_test_point"));
    REQUIRE(0 == conn.get_count("myschema.osm2pgsql_test_line"));
    REQUIRE(0 == conn.get_count("myschema.osm2pgsql_test_polygon"));
    REQUIRE(0 == conn.get_count("myschema.osm2pgsql_test_roads"));
}
