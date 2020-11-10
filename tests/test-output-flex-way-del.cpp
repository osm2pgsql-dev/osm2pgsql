#include <catch.hpp>

#include "common-import.hpp"
#include "common-options.hpp"

static testing::db::import_t db;

static char const *const conf_file = "test_output_flex_way.lua";

static const char *const tdata[] = {
    "n10 v1 dV x10.0 y10.0",         "n11 v1 dV x10.0 y10.1",
    "n12 v1 dV x10.1 y10.0",         "n13 v1 dV x10.1 y10.1",
    "n14 v1 dV x10.2 y10.0",         "n15 v1 dV x10.2 y10.1",
    "n16 v1 dV x10.3 y10.0",         "n17 v1 dV x10.3 y10.1",
    "n18 v1 dV x10.4 y10.0",         "n19 v1 dV x10.4 y10.1",
    "w11 v1 dV Tt1=yes Nn12,n13",    "w12 v1 dV Tt2=yes Nn14,n15",
    "w13 v1 dV Ttboth=yes Nn16,n17", "w14 v1 dV Ttboth=yes Nn18,n19"};

TEST_CASE("delete way: not a member")
{
    options_t options = testing::opt_t().slim().flex(conf_file);

    testing::data_t data{tdata};

    unsigned long num_t1 = 0;
    unsigned long num_tboth = 0;
    SECTION("in none") { data.add("w10 v1 dV Tt=ag Nn10,n11"); }
    SECTION("in t1")
    {
        data.add("w10 v1 dV Tt1=yes Nn10,n11");
        ++num_t1;
    }
    SECTION("in tboth")
    {
        data.add("w10 v1 dV Ttboth=yes Nn10,n11");
        ++num_tboth;
    }

    data.add("r30 v1 dV Tt=ag Mw11@,w12@mark,w13@,w14@mark");
    REQUIRE_NOTHROW(db.run_import(options, data()));

    auto conn = db.db().connect();

    CHECK(1 + num_t1 == conn.get_count("osm2pgsql_test_t1"));
    CHECK(1 == conn.get_count("osm2pgsql_test_t2"));
    CHECK(2 + num_tboth == conn.get_count("osm2pgsql_test_tboth"));

    options.append = true;

    REQUIRE_NOTHROW(db.run_import(options, "w10 v2 dD"));

    CHECK(1 == conn.get_count("osm2pgsql_test_t1"));
    CHECK(1 == conn.get_count("osm2pgsql_test_t2"));
    CHECK(2 == conn.get_count("osm2pgsql_test_tboth"));
}

TEST_CASE("delete way: relation member")
{
    options_t options = testing::opt_t().slim().flex(conf_file);

    testing::data_t data{tdata};

    unsigned long num_t1 = 0;
    unsigned long num_t2 = 0;
    unsigned long num_tboth = 0;

    SECTION("in none")
    {
        data.add({"w10 v1 dV Tt=ag Nn10,n11",
                  "r30 v1 dV Tt=ag Mw10@,w11@,w12@mark,w13@,w14@mark"});
    }
    SECTION("in t1")
    {
        data.add({"w10 v1 dV Tt1=yes Nn10,n11",
                  "r30 v1 dV Tt=ag Mw10@,w11@,w12@mark,w13@,w14@mark"});
        ++num_t1;
    }
    SECTION("in t2")
    {
        data.add({"w10 v1 dV Tt2=yes Nn10,n11",
                  "r30 v1 dV Tt=ag Mw10@mark,w11@,w12@mark,w13@,w14@mark"});
        ++num_t2;
    }
    SECTION("in t1 and t2")
    {
        data.add({"w10 v1 dV Tt1=yes,t2=yes Nn10,n11",
                  "r30 v1 dV Tt=ag Mw10@mark,w11@,w12@mark,w13@,w14@mark"});
        ++num_t1;
        ++num_t2;
    }
    SECTION("in tboth (without mark)")
    {
        data.add({"w10 v1 dV Ttboth=yes Nn10,n11",
                  "r30 v1 dV Tt=ag Mw10@,w11@,w12@mark,w13@,w14@mark"});
        ++num_tboth;
    }
    SECTION("in tboth (with mark)")
    {
        data.add({"w10 v1 dV Ttboth=yes Nn10,n11",
                  "r30 v1 dV Tt=ag Mw10@mark,w11@,w12@mark,w13@,w14@mark"});
        ++num_tboth;
    }

    REQUIRE_NOTHROW(db.run_import(options, data()));

    auto conn = db.db().connect();

    CHECK(1 + num_t1 == conn.get_count("osm2pgsql_test_t1"));
    CHECK(1 + num_t2 == conn.get_count("osm2pgsql_test_t2"));
    CHECK(2 + num_tboth == conn.get_count("osm2pgsql_test_tboth"));

    options.append = true;

    REQUIRE_NOTHROW(db.run_import(options, "w10 v2 dD"));

    CHECK(1 == conn.get_count("osm2pgsql_test_t1"));
    CHECK(1 == conn.get_count("osm2pgsql_test_t2"));
    CHECK(2 == conn.get_count("osm2pgsql_test_tboth"));
}
