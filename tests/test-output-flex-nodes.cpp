#include <catch.hpp>

#include "common-import.hpp"
#include "common-options.hpp"

static testing::db::import_t db;

static char const *const conf_file = "test_output_flex_nodes.lua";

TEST_CASE("add nodes")
{
    options_t options = testing::opt_t().slim().flex(conf_file);

    REQUIRE_NOTHROW(db.run_import(options,
                                  "n10 v1 dV x10.0 y10.0\n"
                                  "n11 v1 dV Tt1=yes x10.0 y10.1\n"
                                  "n12 v1 dV Tt2=yes x10.0 y10.2\n"
                                  "n13 v1 dV Tt1=yes,t2=yes x10.0 y10.2\n"));

    auto conn = db.db().connect();

    CHECK(2 == conn.get_count("osm2pgsql_test_t1"));
    CHECK(2 == conn.get_count("osm2pgsql_test_t2"));
    CHECK(1 == conn.get_count("osm2pgsql_test_t1", "node_id = 11"));
    CHECK(1 == conn.get_count("osm2pgsql_test_t1", "node_id = 13"));

    options.append = true;

    REQUIRE_NOTHROW(db.run_import(options,
                                  "n14 v1 dV x11.0 y10.0\n"
                                  "n15 v1 dV Tt1=yes x11.0 y10.1\n"
                                  "n16 v1 dV Tt2=yes x11.0 y10.2\n"
                                  "n17 v1 dV Tt1=yes,t2=yes x11.0 y10.2\n"));

    CHECK(4 == conn.get_count("osm2pgsql_test_t1"));
    CHECK(4 == conn.get_count("osm2pgsql_test_t2"));
    CHECK(1 == conn.get_count("osm2pgsql_test_t1", "node_id = 11"));
    CHECK(1 == conn.get_count("osm2pgsql_test_t1", "node_id = 13"));
    CHECK(1 == conn.get_count("osm2pgsql_test_t1", "node_id = 15"));
    CHECK(1 == conn.get_count("osm2pgsql_test_t1", "node_id = 17"));
}

enum class node_relationship
{
    none,
    in_way,
    in_relation
};

template <node_relationship R>
struct node_rel
{
    static constexpr const node_relationship rs = R;
};

using node_rel_none = node_rel<node_relationship::none>;
using node_rel_in_way = node_rel<node_relationship::in_way>;
using node_rel_in_relation = node_rel<node_relationship::in_relation>;

TEMPLATE_TEST_CASE("change nodes", "", node_rel_none, node_rel_in_way,
                   node_rel_in_relation)
{
    options_t options = testing::opt_t().slim().flex(conf_file);

    REQUIRE_NOTHROW(db.run_import(options,
                                  "n10 v1 dV x10.0 y10.0\n"
                                  "n11 v1 dV Tt1=yes x10.0 y10.1\n"
                                  "n12 v1 dV Tt2=yes x10.0 y10.2\n"
                                  "n13 v1 dV Tt1=yes,t2=yes x10.0 y10.2\n"

                                  "n14 v1 dV x11.0 y10.0\n"
                                  "n15 v1 dV Tt1=yes x11.0 y10.1\n"
                                  "n16 v1 dV Tt1=yes,t2=yes x11.0 y10.2\n"));

    options.append = true;

    if (TestType{}.rs == node_relationship::in_way) {
        REQUIRE_NOTHROW(db.run_import(options, "w20 v1 dV Nn14,n15,n16\n"));
    } else if (TestType{}.rs == node_relationship::in_relation) {
        REQUIRE_NOTHROW(db.run_import(options, "r30 v1 dV Mn14@,n15@,n16@\n"));
    }

    auto conn = db.db().connect();

    CHECK(4 == conn.get_count("osm2pgsql_test_t1"));
    CHECK(3 == conn.get_count("osm2pgsql_test_t2"));
    CHECK(1 == conn.get_count("osm2pgsql_test_t1", "node_id = 11"));
    CHECK(1 == conn.get_count("osm2pgsql_test_t1", "node_id = 13"));
    CHECK(1 == conn.get_count("osm2pgsql_test_t1", "node_id = 15"));
    CHECK(1 == conn.get_count("osm2pgsql_test_t1", "node_id = 16"));

    SECTION("no tag, add tag t1")
    {
        REQUIRE_NOTHROW(
            db.run_import(options, "n14 v2 dV Tt1=yes x11.0 y10.0\n"));
        CHECK(5 == conn.get_count("osm2pgsql_test_t1"));
        CHECK(3 == conn.get_count("osm2pgsql_test_t2"));
    }

    SECTION("no tag, add tag t1, t2")
    {
        REQUIRE_NOTHROW(
            db.run_import(options, "n14 v2 dV Tt1=yes,t2=yes x11.0 y10.0\n"));
        CHECK(5 == conn.get_count("osm2pgsql_test_t1"));
        CHECK(4 == conn.get_count("osm2pgsql_test_t2"));
    }

    SECTION("one tag, remove tag t1")
    {
        REQUIRE_NOTHROW(db.run_import(options, "n15 v2 dV x11.0 y10.0\n"));
        CHECK(3 == conn.get_count("osm2pgsql_test_t1"));
        CHECK(3 == conn.get_count("osm2pgsql_test_t2"));
    }

    SECTION("one tag, change tag t1 to t2")
    {
        REQUIRE_NOTHROW(
            db.run_import(options, "n15 v2 dV Tt2=yes x11.0 y10.0\n"));
        CHECK(3 == conn.get_count("osm2pgsql_test_t1"));
        CHECK(4 == conn.get_count("osm2pgsql_test_t2"));
    }

    SECTION("one tag, add tag t2")
    {
        REQUIRE_NOTHROW(
            db.run_import(options, "n15 v2 dV Tt1=yes,t2=yes x11.0 y10.0\n"));
        CHECK(4 == conn.get_count("osm2pgsql_test_t1"));
        CHECK(4 == conn.get_count("osm2pgsql_test_t2"));
    }

    SECTION("two tags, remove tag t1 and t2")
    {
        REQUIRE_NOTHROW(db.run_import(options, "n16 v2 dV x11.0 y10.0\n"));
        CHECK(3 == conn.get_count("osm2pgsql_test_t1"));
        CHECK(2 == conn.get_count("osm2pgsql_test_t2"));
    }

    SECTION("two tags, remove only tag t1 not t2")
    {
        REQUIRE_NOTHROW(
            db.run_import(options, "n16 v2 dV Tt2=yes x11.0 y10.0\n"));
        CHECK(3 == conn.get_count("osm2pgsql_test_t1"));
        CHECK(3 == conn.get_count("osm2pgsql_test_t2"));
    }
}

TEMPLATE_TEST_CASE("delete nodes", "", node_rel_none, node_rel_in_way,
                   node_rel_in_relation)
{
    options_t options = testing::opt_t().slim().flex(conf_file);

    REQUIRE_NOTHROW(db.run_import(options,
                                  "n10 v1 dV x10.0 y10.0\n"
                                  "n11 v1 dV Tt1=yes x10.0 y10.1\n"
                                  "n12 v1 dV Tt2=yes x10.0 y10.2\n"
                                  "n13 v1 dV Tt1=yes,t2=yes x10.0 y10.2\n"

                                  "n14 v1 dV x11.0 y10.0\n"
                                  "n15 v1 dV Tt1=yes x11.0 y10.1\n"
                                  "n16 v1 dV Tt1=yes,t2=yes x11.0 y10.2\n"));

    options.append = true;

    if (TestType{}.rs == node_relationship::in_way) {
        REQUIRE_NOTHROW(db.run_import(options, "w20 v1 dV Nn14,n15,n16\n"));
    } else if (TestType{}.rs == node_relationship::in_relation) {
        REQUIRE_NOTHROW(db.run_import(options, "r30 v1 dV Mn14@,n15@,n16@\n"));
    }

    auto conn = db.db().connect();

    CHECK(4 == conn.get_count("osm2pgsql_test_t1"));
    CHECK(3 == conn.get_count("osm2pgsql_test_t2"));
    CHECK(1 == conn.get_count("osm2pgsql_test_t1", "node_id = 11"));
    CHECK(1 == conn.get_count("osm2pgsql_test_t1", "node_id = 13"));
    CHECK(1 == conn.get_count("osm2pgsql_test_t1", "node_id = 15"));
    CHECK(1 == conn.get_count("osm2pgsql_test_t1", "node_id = 16"));

    REQUIRE_NOTHROW(db.run_import(options, "n14 v2 dD\n"
                                           "n15 v2 dD\n"
                                           "n16 v2 dD\n"));

    CHECK(2 == conn.get_count("osm2pgsql_test_t1"));
    CHECK(2 == conn.get_count("osm2pgsql_test_t2"));
}
