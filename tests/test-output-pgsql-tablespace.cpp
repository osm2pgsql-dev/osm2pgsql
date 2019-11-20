#include <catch.hpp>

#include "common-import.hpp"
#include "common-options.hpp"

static testing::db::import_t db;

static void require_tables(pg::conn_t const &conn)
{
    conn.require_has_table("osm2pgsql_test_point");
    conn.require_has_table("osm2pgsql_test_line");
    conn.require_has_table("osm2pgsql_test_polygon");
    conn.require_has_table("osm2pgsql_test_roads");
}

TEST_CASE("simple import with tables spaces")
{
    {
        auto conn = db.db().connect();
        REQUIRE(1 ==
                conn.get_count("pg_tablespace", "spcname = 'tablespacetest'"));
    }

    options_t options = testing::opt_t().slim();
    options.tblsslim_index = "tablespacetest";
    options.tblsslim_data = "tablespacetest";

    REQUIRE_NOTHROW(db.run_file(options, "liechtenstein-2013-08-03.osm.pbf"));

    auto conn = db.db().connect();
    require_tables(conn);

    REQUIRE(1342 == conn.get_count("osm2pgsql_test_point"));
    REQUIRE(3231 == conn.get_count("osm2pgsql_test_line"));
    REQUIRE(375 == conn.get_count("osm2pgsql_test_roads"));
    REQUIRE(4130 == conn.get_count("osm2pgsql_test_polygon"));
}
