#include <catch.hpp>

#include "common-import.hpp"
#include "common-options.hpp"

static testing::db::import_t db;

static char const *const conf_file = "test_output_flex_attr.lua";
static char const *const table = "osm2pgsql_test_attr";

TEST_CASE("without extra_attributes")
{
    options_t options = testing::opt_t().slim().flex(conf_file);

    REQUIRE_NOTHROW(
        db.run_import(options, "n10 v1 dV x10.0 y10.0\n"
                               "n11 v1 dV x10.0 y10.2\n"
                               "n12 v1 dV x10.2 y10.2\n"
                               "w20 v1 dV c31 t2020-01-12T12:34:56Z i17 utest "
                               "Thighway=primary Nn10,n11,n12\n"));

    auto conn = db.db().connect();

    CHECK(1 == conn.get_count(table));
    CHECK(1 == conn.get_count(table, "tags->'highway' = 'primary'"));
    CHECK(0 == conn.get_count(table, "version = 1"));
    CHECK(0 == conn.get_count(table, "changeset = 31"));
    CHECK(0 == conn.get_count(table, "timestamp = 1578832496"));
    CHECK(0 == conn.get_count(table, "uid = 17"));
    CHECK(0 == conn.get_count(table, "\"user\" = 'test'"));

    options.append = true;

    REQUIRE_NOTHROW(db.run_import(options, "n10 v2 dV x11.0 y11.0\n"));

    CHECK(1 == conn.get_count(table));
    CHECK(1 == conn.get_count(table, "tags->'highway' = 'primary'"));
    CHECK(0 == conn.get_count(table, "version = 1"));
    CHECK(0 == conn.get_count(table, "changeset = 31"));
    CHECK(0 == conn.get_count(table, "timestamp = 1578832496"));
    CHECK(0 == conn.get_count(table, "uid = 17"));
    CHECK(0 == conn.get_count(table, "\"user\" = 'test'"));
}

TEST_CASE("with extra_attributes")
{
    options_t options =
        testing::opt_t().extra_attributes().slim().flex(conf_file);

    REQUIRE_NOTHROW(
        db.run_import(options, "n10 v1 dV x10.0 y10.0\n"
                               "n11 v1 dV x10.0 y10.2\n"
                               "n12 v1 dV x10.2 y10.2\n"
                               "w20 v1 dV c31 t2020-01-12T12:34:56Z i17 utest "
                               "Thighway=primary Nn10,n11,n12\n"));

    auto conn = db.db().connect();

    CHECK(1 == conn.get_count(table));
    CHECK(1 == conn.get_count(table, "tags->'highway' = 'primary'"));
    CHECK(1 == conn.get_count(table, "version = 1"));
    CHECK(1 == conn.get_count(table, "changeset = 31"));
    CHECK(1 == conn.get_count(table, "timestamp = 1578832496"));
    CHECK(1 == conn.get_count(table, "uid = 17"));
    CHECK(1 == conn.get_count(table, "\"user\" = 'test'"));

    options.append = true;

    REQUIRE_NOTHROW(db.run_import(options, "n10 v2 dV x11.0 y11.0\n"));

    CHECK(1 == conn.get_count(table));
    CHECK(1 == conn.get_count(table, "tags->'highway' = 'primary'"));
    CHECK(1 == conn.get_count(table, "version = 1"));
    CHECK(1 == conn.get_count(table, "changeset = 31"));
    CHECK(1 == conn.get_count(table, "timestamp = 1578832496"));
    CHECK(1 == conn.get_count(table, "uid = 17"));
    CHECK(1 == conn.get_count(table, "\"user\" = 'test'"));
}
