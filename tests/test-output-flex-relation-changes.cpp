#include <catch.hpp>

#include "common-import.hpp"
#include "common-options.hpp"

static testing::db::import_t db;

static char const *const conf_file = "test_output_flex_relation_changes.lua";

TEST_CASE("changing type adds relation")
{
    options_t options = testing::opt_t().slim().flex(conf_file);

    REQUIRE_NOTHROW(db.run_import(options, "n10 v1 dV x10.0 y10.0\n"
                                           "n11 v1 dV x10.0 y10.1\n"
                                           "n12 v1 dV x10.1 y10.1\n"
                                           "n13 v1 dV x10.1 y10.0\n"
                                           "w20 v1 dV Nn10,n11,n12\n"
                                           "w21 v1 dV Nn12,n13,n10\n"
                                           "r30 v1 dV Mw20@,w21@\n"));

    auto conn = db.db().connect();

    CHECK(0 == conn.get_count("osm2pgsql_test_relations"));

    options.append = true;

    REQUIRE_NOTHROW(
        db.run_import(options, "r30 v2 dV Ttype=multipolygon Mw20@,w21@\n"));

    CHECK(1 == conn.get_count("osm2pgsql_test_relations"));
}

TEST_CASE("changed way adds relation")
{
    options_t options = testing::opt_t().slim().flex(conf_file);

    REQUIRE_NOTHROW(db.run_import(options,
                                  "n10 v1 dV x10.0 y10.0\n"
                                  "n11 v1 dV x10.0 y10.1\n"
                                  "n12 v1 dV x10.1 y10.1\n"
                                  "n13 v1 dV x10.1 y10.0\n"
                                  "w20 v1 dV Nn10,n11,n12\n"
                                  "w21 v1 dV Nn12,n13\n"
                                  "r30 v1 dV Ttype=multipolygon Mw20@,w21@\n"));

    auto conn = db.db().connect();

    CHECK(0 == conn.get_count("osm2pgsql_test_relations"));

    options.append = true;

    REQUIRE_NOTHROW(db.run_import(options, "w21 v2 dV Nn12,n13,n10\n"));

    CHECK(1 == conn.get_count("osm2pgsql_test_relations"));
}

TEST_CASE("changed node adds relation")
{
    options_t options = testing::opt_t().slim().flex(conf_file);

    REQUIRE_NOTHROW(db.run_import(options,
                                  "n10 v1 dV x10.0 y10.0\n"
                                  "n11 v1 dV x10.0 y10.1\n"
                                  "n12 v1 dV x10.0 y10.1\n"
                                  "w20 v1 dV Nn10,n11,n12\n"
                                  "w21 v1 dV Nn12,n10\n"
                                  "r30 v1 dV Ttype=multipolygon Mw20@,w21@\n"));

    auto conn = db.db().connect();

    CHECK(0 == conn.get_count("osm2pgsql_test_relations"));

    options.append = true;

    REQUIRE_NOTHROW(db.run_import(options, "n12 v2 dV x10.1 y10.1\n"));

    CHECK(1 == conn.get_count("osm2pgsql_test_relations"));
}

TEST_CASE("changed member list adds relation")
{
    options_t options = testing::opt_t().slim().flex(conf_file);

    REQUIRE_NOTHROW(db.run_import(options,
                                  "n10 v1 dV x10.0 y10.0\n"
                                  "n11 v1 dV x10.0 y10.1\n"
                                  "n12 v1 dV x10.1 y10.1\n"
                                  "w20 v1 dV Nn10,n11,n12\n"
                                  "w21 v1 dV Nn12,n10\n"
                                  "r30 v1 dV Ttype=multipolygon Mw20@\n"));

    auto conn = db.db().connect();

    CHECK(0 == conn.get_count("osm2pgsql_test_relations"));

    options.append = true;

    REQUIRE_NOTHROW(
        db.run_import(options, "r30 v2 dV Ttype=multipolygon Mw20@,w21@\n"));

    CHECK(1 == conn.get_count("osm2pgsql_test_relations"));
}

TEST_CASE("changing type deletes relation")
{
    options_t options = testing::opt_t().slim().flex(conf_file);

    REQUIRE_NOTHROW(db.run_import(options,
                                  "n10 v1 dV x10.0 y10.0\n"
                                  "n11 v1 dV x10.0 y10.1\n"
                                  "n12 v1 dV x10.1 y10.1\n"
                                  "n13 v1 dV x10.1 y10.0\n"
                                  "w20 v1 dV Nn10,n11,n12\n"
                                  "w21 v1 dV Nn12,n13,n10\n"
                                  "r30 v1 dV Ttype=multipolygon Mw20@,w21@\n"));

    auto conn = db.db().connect();

    CHECK(1 == conn.get_count("osm2pgsql_test_relations"));

    options.append = true;

    REQUIRE_NOTHROW(db.run_import(options, "r30 v2 dV Mw20@,w21@\n"));

    CHECK(0 == conn.get_count("osm2pgsql_test_relations"));
}

TEST_CASE("changed way deletes relation")
{
    options_t options = testing::opt_t().slim().flex(conf_file);

    REQUIRE_NOTHROW(db.run_import(options,
                                  "n10 v1 dV x10.0 y10.0\n"
                                  "n11 v1 dV x10.0 y10.1\n"
                                  "n12 v1 dV x10.1 y10.1\n"
                                  "n13 v1 dV x10.1 y10.0\n"
                                  "w20 v1 dV Nn10,n11,n12\n"
                                  "w21 v1 dV Nn12,n13,n10\n"
                                  "r30 v1 dV Ttype=multipolygon Mw20@,w21@\n"));

    auto conn = db.db().connect();

    CHECK(1 == conn.get_count("osm2pgsql_test_relations"));

    options.append = true;

    REQUIRE_NOTHROW(db.run_import(options, "w21 v2 dV Nn12,n13\n"));

    CHECK(0 == conn.get_count("osm2pgsql_test_relations"));
}

TEST_CASE("changed node deletes relation")
{
    options_t options = testing::opt_t().slim().flex(conf_file);

    REQUIRE_NOTHROW(db.run_import(options,
                                  "n10 v1 dV x10.0 y10.0\n"
                                  "n11 v1 dV x10.0 y10.1\n"
                                  "n12 v1 dV x10.1 y10.1\n"
                                  "w20 v1 dV Nn10,n11,n12\n"
                                  "w21 v1 dV Nn12,n10\n"
                                  "r30 v1 dV Ttype=multipolygon Mw20@,w21@\n"));

    auto conn = db.db().connect();

    CHECK(1 == conn.get_count("osm2pgsql_test_relations"));

    options.append = true;

    REQUIRE_NOTHROW(db.run_import(options, "n12 v2 dV x10.0 y10.1\n"));

    CHECK(0 == conn.get_count("osm2pgsql_test_relations"));
}

TEST_CASE("changed member list deletes relation")
{
    options_t options = testing::opt_t().slim().flex(conf_file);

    REQUIRE_NOTHROW(db.run_import(options,
                                  "n10 v1 dV x10.0 y10.0\n"
                                  "n11 v1 dV x10.0 y10.1\n"
                                  "n12 v1 dV x10.1 y10.1\n"
                                  "w20 v1 dV Nn10,n11,n12\n"
                                  "w21 v1 dV Nn12,n10\n"
                                  "r30 v1 dV Ttype=multipolygon Mw20@,w21@\n"));

    auto conn = db.db().connect();

    CHECK(1 == conn.get_count("osm2pgsql_test_relations"));

    options.append = true;

    REQUIRE_NOTHROW(
        db.run_import(options, "r30 v2 dV Ttype=multipolygon Mw20@\n"));

    CHECK(0 == conn.get_count("osm2pgsql_test_relations"));
}

TEST_CASE("changing tag keeps relation")
{
    options_t options = testing::opt_t().slim().flex(conf_file);

    REQUIRE_NOTHROW(db.run_import(
        options, "n10 v1 dV x10.0 y10.0\n"
                 "n11 v1 dV x10.0 y10.1\n"
                 "n12 v1 dV x10.1 y10.1\n"
                 "n13 v1 dV x10.1 y10.0\n"
                 "w20 v1 dV Nn10,n11,n12\n"
                 "w21 v1 dV Nn12,n13,n10\n"
                 "r30 v1 dV Ttype=multipolygon,natural=wood Mw20@,w21@\n"));

    auto conn = db.db().connect();

    CHECK(1 == conn.get_count("osm2pgsql_test_relations"));

    options.append = true;

    REQUIRE_NOTHROW(db.run_import(
        options, "r30 v2 dV Ttype=multipolygon,landuse=forest Mw20@,w21@\n"));

    CHECK(1 == conn.get_count("osm2pgsql_test_relations"));
}

TEST_CASE("changed way keeps relation")
{
    options_t options = testing::opt_t().slim().flex(conf_file);

    REQUIRE_NOTHROW(db.run_import(options,
                                  "n10 v1 dV x10.0 y10.0\n"
                                  "n11 v1 dV x10.0 y10.1\n"
                                  "n12 v1 dV x10.1 y10.1\n"
                                  "n13 v1 dV x10.1 y10.0\n"
                                  "w20 v1 dV Nn10,n11,n12\n"
                                  "w21 v1 dV Nn12,n13,n10\n"
                                  "r30 v1 dV Ttype=multipolygon Mw20@,w21@\n"));

    auto conn = db.db().connect();

    CHECK(1 == conn.get_count("osm2pgsql_test_relations"));

    options.append = true;

    REQUIRE_NOTHROW(db.run_import(options, "w21 v2 dV Nn10,n13,n12\n"));

    CHECK(1 == conn.get_count("osm2pgsql_test_relations"));
}

TEST_CASE("changed node keeps relation")
{
    options_t options = testing::opt_t().slim().flex(conf_file);

    REQUIRE_NOTHROW(db.run_import(options,
                                  "n10 v1 dV x10.0 y10.0\n"
                                  "n11 v1 dV x10.0 y10.1\n"
                                  "n12 v1 dV x10.1 y10.1\n"
                                  "w20 v1 dV Nn10,n11,n12\n"
                                  "w21 v1 dV Nn12,n10\n"
                                  "r30 v1 dV Ttype=multipolygon Mw20@,w21@\n"));

    auto conn = db.db().connect();

    CHECK(1 == conn.get_count("osm2pgsql_test_relations"));

    options.append = true;

    REQUIRE_NOTHROW(db.run_import(options, "n12 v2 dV x10.2 y10.1\n"));

    CHECK(1 == conn.get_count("osm2pgsql_test_relations"));
}

TEST_CASE("changed member list keeps relation")
{
    options_t options = testing::opt_t().slim().flex(conf_file);

    REQUIRE_NOTHROW(db.run_import(options,
                                  "n10 v1 dV x10.0 y10.0\n"
                                  "n11 v1 dV x10.0 y10.1\n"
                                  "n12 v1 dV x10.1 y10.1\n"
                                  "w20 v1 dV Nn10,n11,n12\n"
                                  "w21 v1 dV Nn12,n10\n"
                                  "r30 v1 dV Ttype=multipolygon Mw20@,w21@\n"));

    auto conn = db.db().connect();

    CHECK(1 == conn.get_count("osm2pgsql_test_relations"));

    options.append = true;

    REQUIRE_NOTHROW(
        db.run_import(options, "r30 v2 dV Ttype=multipolygon Mw21@,w20@\n"));

    CHECK(1 == conn.get_count("osm2pgsql_test_relations"));
}
