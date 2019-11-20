#include <catch.hpp>

#include <options.hpp>

/**
 * Tests that the conninfo strings are appropriately generated
 * This test is stricter than it needs to be, as it also cares about order,
 * but the current implementation always uses the same order, and attempting to
 * parse a conninfo string is complex.
 */
TEST_CASE("Connection info parsing", "[NoDB]")
{
    database_options_t db;
    CHECK(db.conninfo() == "fallback_application_name='osm2pgsql'");
    db.db = "foo";
    CHECK(db.conninfo() ==
          "fallback_application_name='osm2pgsql' dbname='foo'");

    db = database_options_t();
    db.username = "bar";
    CHECK(db.conninfo() == "fallback_application_name='osm2pgsql' user='bar'");

    db = database_options_t();
    db.password = "bar";
    CHECK(db.conninfo() ==
          "fallback_application_name='osm2pgsql' password='bar'");

    db = database_options_t();
    db.host = "bar";
    CHECK(db.conninfo() == "fallback_application_name='osm2pgsql' host='bar'");

    db = database_options_t();
    db.port = "bar";
    CHECK(db.conninfo() == "fallback_application_name='osm2pgsql' port='bar'");

    db = database_options_t();
    db.db = "foo";
    db.username = "bar";
    db.password = "baz";
    db.host = "bzz";
    db.port = "123";
    CHECK(db.conninfo() == "fallback_application_name='osm2pgsql' dbname='foo' "
                           "user='bar' password='baz' host='bzz' port='123'");
}
