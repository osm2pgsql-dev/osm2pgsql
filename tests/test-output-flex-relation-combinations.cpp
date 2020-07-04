#include <catch.hpp>

#include "common-import.hpp"
#include "common-options.hpp"

static testing::db::import_t db;

static char const *const conf_file =
    "test_output_flex_relation_combinations.lua";

TEST_CASE("adding common way to relation")
{
    options_t options = testing::opt_t().slim().flex(conf_file);

    REQUIRE_NOTHROW(db.run_import(options, "n10 v1 dV x10.0 y10.0\n"
                                           "n11 v1 dV x10.0 y10.1\n"
                                           "n12 v1 dV x10.1 y10.1\n"
                                           "n13 v1 dV x10.1 y10.0\n"
                                           "w20 v1 dV Nn10,n11\n"
                                           "w21 v1 dV Nn12,n13\n"
                                           "r30 v1 dV Ta=b Mw20@\n"
                                           "r31 v1 dV Ta=b Mw21@\n"));

    auto conn = db.db().connect();

    CHECK(2 == conn.get_count("osm2pgsql_test_relations"));
    CHECK(1 == conn.get_count("osm2pgsql_test_relations", "relation_id = 30"));
    CHECK(1 == conn.get_count("osm2pgsql_test_relations", "relation_id = 31"));

    options.append = true;

    REQUIRE_NOTHROW(db.run_import(options, "r31 v2 dV Ta=b Mw20@,w21@\n"));

    CHECK(2 == conn.get_count("osm2pgsql_test_relations"));
    CHECK(1 == conn.get_count("osm2pgsql_test_relations", "relation_id = 30"));
    CHECK(1 == conn.get_count("osm2pgsql_test_relations", "relation_id = 31"));
}

TEST_CASE("remove common way from relation")
{
    options_t options = testing::opt_t().slim().flex(conf_file);

    REQUIRE_NOTHROW(db.run_import(options, "w20 v1 dV Nn10,n11\n"
                                           "w21 v1 dV Nn12,n13\n"
                                           "r30 v1 dV Ta=b Mw20@\n"
                                           "r31 v1 dV Ta=b Mw20@,w21@\n"));

    auto conn = db.db().connect();

    CHECK(2 == conn.get_count("osm2pgsql_test_relations"));
    CHECK(1 == conn.get_count("osm2pgsql_test_relations", "relation_id = 30"));
    CHECK(1 == conn.get_count("osm2pgsql_test_relations", "relation_id = 31"));

    options.append = true;

    REQUIRE_NOTHROW(db.run_import(options, "r31 v2 dV Ta=b Mw21@\n"));

    CHECK(2 == conn.get_count("osm2pgsql_test_relations"));
    CHECK(1 == conn.get_count("osm2pgsql_test_relations", "relation_id = 30"));
    CHECK(1 == conn.get_count("osm2pgsql_test_relations", "relation_id = 31"));
}

TEST_CASE("change common way in relation")
{
    options_t options = testing::opt_t().slim().flex(conf_file);

    REQUIRE_NOTHROW(db.run_import(options, "w20 v1 dV Nn10,n11\n"
                                           "w21 v1 dV Nn12,n13\n"
                                           "r30 v1 dV Ta=b Mw20@\n"
                                           "r31 v1 dV Ta=b Mw20@,w21@\n"));

    auto conn = db.db().connect();

    CHECK(2 == conn.get_count("osm2pgsql_test_relations"));
    CHECK(1 == conn.get_count("osm2pgsql_test_relations", "relation_id = 30"));
    CHECK(1 == conn.get_count("osm2pgsql_test_relations", "relation_id = 31"));

    options.append = true;

    REQUIRE_NOTHROW(db.run_import(options, "r31 v2 dV Ta=c Mw20@,w21@\n"));

    CHECK(2 == conn.get_count("osm2pgsql_test_relations"));
    CHECK(1 == conn.get_count("osm2pgsql_test_relations", "relation_id = 30"));
    CHECK(1 == conn.get_count("osm2pgsql_test_relations", "relation_id = 31"));
}
