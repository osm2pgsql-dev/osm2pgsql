#include <catch.hpp>

#include "common-import.hpp"
#include "pgsql.hpp"

static testing::db::import_t db;

TEST_CASE("Tablespace clause with no tablespace")
{
    REQUIRE(tablespace_clause("") == "");
}

TEST_CASE("Tablespace clause with tablespace")
{
    REQUIRE(tablespace_clause("foo") == R"( TABLESPACE "foo")");
}

TEST_CASE("Table name without schema")
{
    REQUIRE(qualified_name("", "foo") == R"("foo")");
}

TEST_CASE("Table name with schema")
{
    REQUIRE(qualified_name("osm", "foo") == R"("osm"."foo")");
}

TEST_CASE("PostGIS version")
{
    auto conn = db.db().connect();
    auto const postgis_version = get_postgis_version(conn);
    REQUIRE(postgis_version.major >= 2);
}
