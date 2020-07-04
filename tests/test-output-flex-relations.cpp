#include <catch.hpp>

#include "common-import.hpp"
#include "common-options.hpp"

static testing::db::import_t db;

static char const *const conf_file = "test_output_flex_relations.lua";

TEST_CASE("add relations")
{
    options_t options = testing::opt_t().slim().flex(conf_file);

    REQUIRE_NOTHROW(db.run_import(options, "r30 v1 dV\n"
                                           "r31 v1 dV Tt1=yes\n"
                                           "r32 v1 dV Tt2=yes\n"
                                           "r33 v1 dV Tt1=yes,t2=yes\n"));

    auto conn = db.db().connect();

    CHECK(2 == conn.get_count("osm2pgsql_test_t1"));
    CHECK(2 == conn.get_count("osm2pgsql_test_t2"));
    CHECK(1 == conn.get_count("osm2pgsql_test_t1", "relation_id = 31"));
    CHECK(1 == conn.get_count("osm2pgsql_test_t1", "relation_id = 33"));

    options.append = true;

    REQUIRE_NOTHROW(db.run_import(options, "r34 v1 dV\n"
                                           "r35 v1 dV Tt1=yes\n"
                                           "r36 v1 dV Tt2=yes\n"
                                           "r37 v1 dV Tt1=yes,t2=yes\n"));

    CHECK(4 == conn.get_count("osm2pgsql_test_t1"));
    CHECK(4 == conn.get_count("osm2pgsql_test_t2"));
    CHECK(1 == conn.get_count("osm2pgsql_test_t1", "relation_id = 31"));
    CHECK(1 == conn.get_count("osm2pgsql_test_t1", "relation_id = 33"));
    CHECK(1 == conn.get_count("osm2pgsql_test_t1", "relation_id = 35"));
    CHECK(1 == conn.get_count("osm2pgsql_test_t1", "relation_id = 37"));
}

TEST_CASE("change relations")
{
    options_t options = testing::opt_t().slim().flex(conf_file);

    REQUIRE_NOTHROW(db.run_import(options, "r30 v1 dV\n"
                                           "r31 v1 dV Tt1=yes\n"
                                           "r32 v1 dV Tt2=yes\n"
                                           "r33 v1 dV Tt1=yes,t2=yes\n"

                                           "r34 v1 dV\n"
                                           "r35 v1 dV Tt1=yes\n"
                                           "r36 v1 dV Tt1=yes,t2=yes\n"));

    options.append = true;

    auto conn = db.db().connect();

    CHECK(4 == conn.get_count("osm2pgsql_test_t1"));
    CHECK(3 == conn.get_count("osm2pgsql_test_t2"));
    CHECK(1 == conn.get_count("osm2pgsql_test_t1", "relation_id = 31"));
    CHECK(1 == conn.get_count("osm2pgsql_test_t1", "relation_id = 33"));
    CHECK(1 == conn.get_count("osm2pgsql_test_t1", "relation_id = 35"));
    CHECK(1 == conn.get_count("osm2pgsql_test_t1", "relation_id = 36"));

    SECTION("no tag, add tag t1")
    {
        REQUIRE_NOTHROW(db.run_import(options, "r34 v2 dV Tt1=yes\n"));
        CHECK(5 == conn.get_count("osm2pgsql_test_t1"));
        CHECK(3 == conn.get_count("osm2pgsql_test_t2"));
    }

    SECTION("no tag, add tag t1, t2")
    {
        REQUIRE_NOTHROW(db.run_import(options, "r34 v2 dV Tt1=yes,t2=yes\n"));
        CHECK(5 == conn.get_count("osm2pgsql_test_t1"));
        CHECK(4 == conn.get_count("osm2pgsql_test_t2"));
    }

    SECTION("one tag, remove tag t1")
    {
        REQUIRE_NOTHROW(db.run_import(options, "r35 v2 dV\n"));
        CHECK(3 == conn.get_count("osm2pgsql_test_t1"));
        CHECK(3 == conn.get_count("osm2pgsql_test_t2"));
    }

    SECTION("one tag, change tag t1 to t2")
    {
        REQUIRE_NOTHROW(db.run_import(options, "r35 v2 dV Tt2=yes\n"));
        CHECK(3 == conn.get_count("osm2pgsql_test_t1"));
        CHECK(4 == conn.get_count("osm2pgsql_test_t2"));
    }

    SECTION("one tag, add tag t2")
    {
        REQUIRE_NOTHROW(db.run_import(options, "r35 v2 dV Tt1=yes,t2=yes\n"));
        CHECK(4 == conn.get_count("osm2pgsql_test_t1"));
        CHECK(4 == conn.get_count("osm2pgsql_test_t2"));
    }

    SECTION("two tags, remove tag t1 and t2")
    {
        REQUIRE_NOTHROW(db.run_import(options, "r36 v2 dV\n"));
        CHECK(3 == conn.get_count("osm2pgsql_test_t1"));
        CHECK(2 == conn.get_count("osm2pgsql_test_t2"));
    }

    SECTION("two tags, remove only tag t1 not t2")
    {
        REQUIRE_NOTHROW(db.run_import(options, "r36 v2 dV Tt2=yes\n"));
        CHECK(3 == conn.get_count("osm2pgsql_test_t1"));
        CHECK(3 == conn.get_count("osm2pgsql_test_t2"));
    }
}

TEST_CASE("delete relation")
{
    options_t options = testing::opt_t().slim().flex(conf_file);

    REQUIRE_NOTHROW(db.run_import(options, "r30 v1 dV\n"
                                           "r31 v1 dV Tt1=yes\n"
                                           "r32 v1 dV Tt2=yes\n"
                                           "r33 v1 dV Tt1=yes,t2=yes\n"

                                           "r34 v1 dV\n"
                                           "r35 v1 dV Tt1=yes\n"
                                           "r36 v1 dV Tt1=yes,t2=yes\n"));

    options.append = true;

    auto conn = db.db().connect();

    CHECK(4 == conn.get_count("osm2pgsql_test_t1"));
    CHECK(3 == conn.get_count("osm2pgsql_test_t2"));
    CHECK(1 == conn.get_count("osm2pgsql_test_t1", "relation_id = 31"));
    CHECK(1 == conn.get_count("osm2pgsql_test_t1", "relation_id = 33"));
    CHECK(1 == conn.get_count("osm2pgsql_test_t1", "relation_id = 35"));
    CHECK(1 == conn.get_count("osm2pgsql_test_t1", "relation_id = 36"));

    REQUIRE_NOTHROW(db.run_import(options, "r34 v2 dD\n"
                                           "r35 v2 dD\n"
                                           "r36 v2 dD\n"));

    CHECK(2 == conn.get_count("osm2pgsql_test_t1"));
    CHECK(2 == conn.get_count("osm2pgsql_test_t2"));
}
