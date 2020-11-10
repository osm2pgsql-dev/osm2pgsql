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

TEST_CASE("change way from t1")
{
    options_t options = testing::opt_t().slim().flex(conf_file);

    testing::data_t data{tdata};
    data.add({"w10 v1 dV Tt1=yes Nn10,n11",
              "r30 v1 dV Tt=ag Mw10@mark,w11@,w12@mark,w13@,w14@mark"});

    REQUIRE_NOTHROW(db.run_import(options, data()));

    auto conn = db.db().connect();

    CHECK(2 == conn.get_count("osm2pgsql_test_t1"));
    CHECK(1 == conn.get_count("osm2pgsql_test_t1", "way_id = 10"));
    CHECK(1 == conn.get_count("osm2pgsql_test_t2"));
    CHECK(2 == conn.get_count("osm2pgsql_test_tboth"));

    options.append = true;

    unsigned long num_t1 = 0;
    unsigned long num_t2 = 0;
    std::string update;
    SECTION("to t2")
    {
        update.append("w10 v1 dV Tt2=yes Nn10,n11");
        --num_t1;
        ++num_t2;
    }
    SECTION("to t1 and t2")
    {
        update.append("w10 v1 dV Tt1=yes,t2=yes Nn10,n11");
        ++num_t2;
    }

    REQUIRE_NOTHROW(db.run_import(options, update.c_str()));

    CHECK(2 + num_t1 == conn.get_count("osm2pgsql_test_t1"));
    CHECK(1 + num_t2 == conn.get_count("osm2pgsql_test_t2"));
    CHECK(1 + num_t1 == conn.get_count("osm2pgsql_test_t1", "way_id = 10"));
    CHECK(0 + num_t2 == conn.get_count("osm2pgsql_test_t2", "way_id = 10"));
    CHECK(2 == conn.get_count("osm2pgsql_test_tboth"));
}

TEST_CASE("change way from t2")
{
    options_t options = testing::opt_t().slim().flex(conf_file);

    testing::data_t data{tdata};
    data.add({"w10 v1 dV Tt2=yes Nn10,n11",
              "r30 v1 dV Tt=ag Mw10@mark,w11@,w12@mark,w13@,w14@mark"});

    REQUIRE_NOTHROW(db.run_import(options, data()));

    auto conn = db.db().connect();

    CHECK(1 == conn.get_count("osm2pgsql_test_t1"));
    CHECK(2 == conn.get_count("osm2pgsql_test_t2"));
    CHECK(1 == conn.get_count("osm2pgsql_test_t2", "way_id = 10"));
    CHECK(2 == conn.get_count("osm2pgsql_test_tboth"));

    options.append = true;

    unsigned long num_t1 = 0;
    unsigned long num_t2 = 0;
    std::string update;
    SECTION("to t1")
    {
        update.append("w10 v1 dV Tt1=yes Nn10,n11");
        --num_t2;
        ++num_t1;
    }
    SECTION("to t1 and t2")
    {
        update.append("w10 v1 dV Tt1=yes,t2=yes Nn10,n11");
        ++num_t1;
    }

    REQUIRE_NOTHROW(db.run_import(options, update.c_str()));

    CHECK(1 + num_t1 == conn.get_count("osm2pgsql_test_t1"));
    CHECK(2 + num_t2 == conn.get_count("osm2pgsql_test_t2"));
    CHECK(0 + num_t1 == conn.get_count("osm2pgsql_test_t1", "way_id = 10"));
    CHECK(1 + num_t2 == conn.get_count("osm2pgsql_test_t2", "way_id = 10"));
    CHECK(2 == conn.get_count("osm2pgsql_test_tboth"));
}

TEST_CASE("change way from t1 and t2")
{
    options_t options = testing::opt_t().slim().flex(conf_file);

    testing::data_t data{tdata};
    data.add({"w10 v1 dV Tt1=yes,t2=yes Nn10,n11",
              "r30 v1 dV Tt=ag Mw10@mark,w11@,w12@mark,w13@,w14@mark"});

    REQUIRE_NOTHROW(db.run_import(options, data()));

    auto conn = db.db().connect();

    CHECK(2 == conn.get_count("osm2pgsql_test_t1"));
    CHECK(1 == conn.get_count("osm2pgsql_test_t1", "way_id = 10"));
    CHECK(2 == conn.get_count("osm2pgsql_test_t2"));
    CHECK(1 == conn.get_count("osm2pgsql_test_t2", "way_id = 10"));
    CHECK(2 == conn.get_count("osm2pgsql_test_tboth"));

    options.append = true;

    unsigned long num_t1 = 0;
    unsigned long num_t2 = 0;
    std::string update;
    SECTION("to t1")
    {
        update.append("w10 v1 dV Tt1=yes Nn10,n11");
        --num_t2;
    }
    SECTION("to t2")
    {
        update.append("w10 v1 dV Tt2=yes Nn10,n11");
        --num_t1;
    }

    REQUIRE_NOTHROW(db.run_import(options, update.c_str()));

    CHECK(2 + num_t1 == conn.get_count("osm2pgsql_test_t1"));
    CHECK(2 + num_t2 == conn.get_count("osm2pgsql_test_t2"));
    CHECK(1 + num_t1 == conn.get_count("osm2pgsql_test_t1", "way_id = 10"));
    CHECK(1 + num_t2 == conn.get_count("osm2pgsql_test_t2", "way_id = 10"));
    CHECK(2 == conn.get_count("osm2pgsql_test_tboth"));
}

TEST_CASE("change valid geom to invalid geom")
{
    options_t options = testing::opt_t().slim().flex(conf_file);

    testing::data_t data{tdata};
    data.add({"w10 v1 dV Tt1=yes,t2=yes,tboth=yes Nn10,n11",
              "r30 v1 dV Tt=ag Mw10@mark,w11@,w12@mark,w13@,w14@mark"});

    REQUIRE_NOTHROW(db.run_import(options, data()));

    auto conn = db.db().connect();

    CHECK(2 == conn.get_count("osm2pgsql_test_t1"));
    CHECK(2 == conn.get_count("osm2pgsql_test_t2"));
    CHECK(1 == conn.get_count("osm2pgsql_test_t2",
                              "way_id = 10 AND rel_ids = '{30}'"));
    CHECK(3 == conn.get_count("osm2pgsql_test_tboth"));
    CHECK(1 == conn.get_count("osm2pgsql_test_tboth",
                              "way_id = 10 AND rel_ids = '{30}'"));

    options.append = true;

    SECTION("change node list to make way invalid")
    {
        REQUIRE_NOTHROW(
            db.run_import(options, "w10 v2 dV Tt1=yes,t2=yes,tboth=yes Nn10"));
    }

    SECTION("change node to make way invalid (n11 same location as n10)")
    {
        REQUIRE_NOTHROW(db.run_import(options, "n11 v2 dV x10.0 y10.0"));
    }

    CHECK(1 == conn.get_count("osm2pgsql_test_t1"));
    CHECK(1 == conn.get_count("osm2pgsql_test_t2"));
    CHECK(0 == conn.get_count("osm2pgsql_test_t2", "way_id = 10"));
    CHECK(2 == conn.get_count("osm2pgsql_test_tboth"));
    CHECK(0 == conn.get_count("osm2pgsql_test_tboth", "way_id = 10"));
}

TEST_CASE("change invalid geom to valid geom")
{
    options_t options = testing::opt_t().slim().flex(conf_file);

    testing::data_t data{tdata};
    data.add({"w10 v1 dV Tt1=yes,t2=yes,tboth=yes Nn10",
              "r30 v1 dV Tt=ag Mw10@mark,w11@,w12@mark,w13@,w14@mark"});

    REQUIRE_NOTHROW(db.run_import(options, data()));

    auto conn = db.db().connect();

    CHECK(1 == conn.get_count("osm2pgsql_test_t1"));
    CHECK(1 == conn.get_count("osm2pgsql_test_t2"));
    CHECK(0 == conn.get_count("osm2pgsql_test_t2", "way_id = 10"));
    CHECK(2 == conn.get_count("osm2pgsql_test_tboth"));
    CHECK(0 == conn.get_count("osm2pgsql_test_tboth", "way_id = 10"));

    options.append = true;

    REQUIRE_NOTHROW(
        db.run_import(options, "w10 v2 dV Tt1=yes,t2=yes,tboth=yes Nn10,n11"));

    CHECK(2 == conn.get_count("osm2pgsql_test_t1"));
    CHECK(2 == conn.get_count("osm2pgsql_test_t2"));
    CHECK(1 == conn.get_count("osm2pgsql_test_t2",
                              "way_id = 10 AND rel_ids = '{30}'"));
    CHECK(3 == conn.get_count("osm2pgsql_test_tboth"));
    CHECK(1 == conn.get_count("osm2pgsql_test_tboth",
                              "way_id = 10 AND rel_ids = '{30}'"));
}
