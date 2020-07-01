#include <catch.hpp>

#include "common-import.hpp"
#include "common-options.hpp"

static testing::db::import_t db;

static char const *const conf_file = "test_output_flex_types.lua";

TEST_CASE("type nil")
{
    testing::opt_t const options = testing::opt_t().flex(conf_file);

    REQUIRE_NOTHROW(
        db.run_import(options, "n10 v1 dV x10.0 y10.0 Ttype=nil\n"));

    auto conn = db.db().connect();

    CHECK(1 == conn.get_count("nodes"));
    CHECK(1 ==
          conn.get_count(
              "nodes",
              "ttext IS NULL AND tbool IS NULL AND tint2 IS NULL "
              "AND tint4 IS NULL AND tint8 IS NULL AND treal IS "
              "NULL AND thstr IS NULL AND tdirn IS NULL AND tsqlt IS NULL"));
}

TEST_CASE("type boolean")
{
    testing::opt_t const options = testing::opt_t().flex(conf_file);

    REQUIRE_NOTHROW(
        db.run_import(options, "n10 v1 dV x10.0 y10.0 Ttype=boolean\n"));

    auto conn = db.db().connect();

    CHECK(2 == conn.get_count("nodes"));
    CHECK(1 == conn.get_count("nodes", "tbool = true AND tint2 = 1 AND tint4 = "
                                       "1 AND tint8 = 1 AND tdirn = 1"));
    CHECK(1 == conn.get_count("nodes", "tbool = false AND tint2 = 0 AND tint4 "
                                       "= 0 AND tint8 = 0 AND tdirn = 0"));
}

TEST_CASE("type boolean in column where it doesn't belong")
{
    testing::opt_t const options = testing::opt_t().flex(conf_file);

    REQUIRE_THROWS(db.run_import(
        options, "n10 v1 dV x10.0 y10.0 Ttype=boolean-fail column=ttext\n"));
    REQUIRE_THROWS(db.run_import(
        options, "n10 v1 dV x10.0 y10.0 Ttype=boolean-fail column=treal\n"));
    REQUIRE_THROWS(db.run_import(
        options, "n10 v1 dV x10.0 y10.0 Ttype=boolean-fail column=thstr\n"));
    REQUIRE_THROWS(db.run_import(
        options, "n10 v1 dV x10.0 y10.0 Ttype=boolean-fail column=tsqlt\n"));

    auto conn = db.db().connect();

    CHECK(0 == conn.get_count("nodes"));
}

TEST_CASE("type number")
{
    testing::opt_t const options = testing::opt_t().flex(conf_file);

    REQUIRE_NOTHROW(
        db.run_import(options, "n10 v1 dV x10.0 y10.0 Ttype=number\n"));

    auto conn = db.db().connect();

    CHECK(19 == conn.get_count("nodes"));

    // clang-format off
    CHECK(1 == conn.get_count("nodes", "split_part(tsqlt, '.', 1) = '-2147483649' AND ttext = tsqlt AND tbool = true  AND tint2 IS NULL  AND tint4 IS NULL       AND tint8 = -2147483649               AND tdirn = -1"));
    CHECK(1 == conn.get_count("nodes", "split_part(tsqlt, '.', 1) = '-2147483648' AND ttext = tsqlt AND tbool = true  AND tint2 IS NULL  AND tint4 = -2147483648 AND tint8 = -2147483648               AND tdirn = -1"));
    CHECK(1 == conn.get_count("nodes", "split_part(tsqlt, '.', 1) = '-2147483647' AND ttext = tsqlt AND tbool = true  AND tint2 IS NULL  AND tint4 = -2147483647 AND tint8 = -2147483647               AND tdirn = -1"));
    CHECK(1 == conn.get_count("nodes", "split_part(tsqlt, '.', 1) = '-32769'      AND ttext = tsqlt AND tbool = true  AND tint2 IS NULL  AND tint4 = -32769      AND tint8 = -32769 AND treal = -32769 AND tdirn = -1"));
    CHECK(1 == conn.get_count("nodes", "split_part(tsqlt, '.', 1) = '-32768'      AND ttext = tsqlt AND tbool = true  AND tint2 = -32768 AND tint4 = -32768      AND tint8 = -32768 AND treal = -32768 AND tdirn = -1"));
    CHECK(1 == conn.get_count("nodes", "split_part(tsqlt, '.', 1) = '-32767'      AND ttext = tsqlt AND tbool = true  AND tint2 = -32767 AND tint4 = -32767      AND tint8 = -32767 AND treal = -32767 AND tdirn = -1"));
    CHECK(1 == conn.get_count("nodes", "split_part(tsqlt, '.', 1) = '-2'          AND ttext = tsqlt AND tbool = true  AND tint2 = -2     AND tint4 = -2          AND tint8 = -2     AND treal =   -2   AND tdirn = -1"));
    CHECK(1 == conn.get_count("nodes", "split_part(tsqlt, '.', 1) = '-1'          AND ttext = tsqlt AND tbool = true  AND tint2 = -1     AND tint4 = -1          AND tint8 = -1     AND treal =   -1   AND tdirn = -1"));
    CHECK(1 == conn.get_count("nodes", "           tsqlt          = '-0.5'        AND ttext = tsqlt AND tbool = true  AND tint2 =  0     AND tint4 =  0          AND tint8 =  0     AND treal = -0.5   AND tdirn = -1"));
    CHECK(1 == conn.get_count("nodes", "split_part(tsqlt, '.', 1) = '0'           AND ttext = tsqlt AND tbool = false AND tint2 =  0     AND tint4 =  0          AND tint8 =  0     AND treal =    0   AND tdirn = 0"));
    CHECK(1 == conn.get_count("nodes", "           tsqlt          = '0.5'         AND ttext = tsqlt AND tbool = true  AND tint2 =  0     AND tint4 =  0          AND tint8 =  0     AND treal =  0.5   AND tdirn = 1"));
    CHECK(1 == conn.get_count("nodes", "split_part(tsqlt, '.', 1) = '1'           AND ttext = tsqlt AND tbool = true  AND tint2 =  1     AND tint4 =  1          AND tint8 =  1     AND treal =    1   AND tdirn = 1"));
    CHECK(1 == conn.get_count("nodes", "split_part(tsqlt, '.', 1) = '2'           AND ttext = tsqlt AND tbool = true  AND tint2 =  2     AND tint4 =  2          AND tint8 =  2     AND treal =    2   AND tdirn = 1"));
    CHECK(1 == conn.get_count("nodes", "split_part(tsqlt, '.', 1) = '32767'       AND ttext = tsqlt AND tbool = true  AND tint2 = 32767  AND tint4 = 32767       AND tint8 = 32767  AND treal = 32767  AND tdirn = 1"));
    CHECK(1 == conn.get_count("nodes", "split_part(tsqlt, '.', 1) = '32768'       AND ttext = tsqlt AND tbool = true  AND tint2 IS NULL  AND tint4 = 32768       AND tint8 = 32768  AND treal = 32768  AND tdirn = 1"));
    CHECK(1 == conn.get_count("nodes", "split_part(tsqlt, '.', 1) = '32769'       AND ttext = tsqlt AND tbool = true  AND tint2 IS NULL  AND tint4 = 32769       AND tint8 = 32769  AND treal = 32769  AND tdirn = 1"));
    CHECK(1 == conn.get_count("nodes", "split_part(tsqlt, '.', 1) = '2147483647'  AND ttext = tsqlt AND tbool = true  AND tint2 IS NULL  AND tint4 = 2147483647  AND tint8 = 2147483647                AND tdirn = 1"));
    CHECK(1 == conn.get_count("nodes", "split_part(tsqlt, '.', 1) = '2147483648'  AND ttext = tsqlt AND tbool = true  AND tint2 IS NULL  AND tint4 IS NULL       AND tint8 = 2147483648                AND tdirn = 1"));
    CHECK(1 == conn.get_count("nodes", "split_part(tsqlt, '.', 1) = '2147483649'  AND ttext = tsqlt AND tbool = true  AND tint2 IS NULL  AND tint4 IS NULL       AND tint8 = 2147483649                AND tdirn = 1"));
    // clang-format on
}

TEST_CASE("type string (with bool)")
{
    testing::opt_t const options = testing::opt_t().flex(conf_file);

    REQUIRE_NOTHROW(
        db.run_import(options, "n10 v1 dV x10.0 y10.0 Ttype=string-bool\n"));

    auto conn = db.db().connect();

    CHECK(9 == conn.get_count("nodes"));
    CHECK(3 == conn.get_count("nodes", "tbool = true  AND ttext = 'istrue'"));
    CHECK(3 == conn.get_count("nodes", "tbool = false AND ttext = 'isfalse'"));
    CHECK(3 == conn.get_count("nodes", "tbool IS NULL AND ttext = 'isnull'"));
}

TEST_CASE("type string (with direction)")
{
    testing::opt_t const options = testing::opt_t().flex(conf_file);

    REQUIRE_NOTHROW(db.run_import(
        options, "n10 v1 dV x10.0 y10.0 Ttype=string-direction\n"));

    auto conn = db.db().connect();

    CHECK(9 == conn.get_count("nodes"));
    CHECK(5 == conn.get_count("nodes", "tdirn = tint2"));
    CHECK(4 == conn.get_count("nodes", "tdirn IS NULL AND tint2 IS NULL"));
}

TEST_CASE("type string (with number)")
{
    testing::opt_t const options = testing::opt_t().flex(conf_file);

    REQUIRE_NOTHROW(db.run_import(
        options, "n10 v1 dV x10.0 y10.0 Ttype=string-with-number\n"));

    auto conn = db.db().connect();

    CHECK(18 == conn.get_count("nodes"));

    // clang-format off
    CHECK(1 == conn.get_count("nodes", "tsqlt = '-2147483649' AND ttext = tsqlt AND tint2 IS NULL  AND tint4 IS NULL       AND tint8 = -2147483649              "));
    CHECK(1 == conn.get_count("nodes", "tsqlt = '-2147483648' AND ttext = tsqlt AND tint2 IS NULL  AND tint4 = -2147483648 AND tint8 = -2147483648              "));
    CHECK(1 == conn.get_count("nodes", "tsqlt = '-2147483647' AND ttext = tsqlt AND tint2 IS NULL  AND tint4 = -2147483647 AND tint8 = -2147483647              "));
    CHECK(1 == conn.get_count("nodes", "tsqlt = '-32769'      AND ttext = tsqlt AND tint2 IS NULL  AND tint4 = -32769      AND tint8 = -32769 AND treal = -32769"));
    CHECK(1 == conn.get_count("nodes", "tsqlt = '-32768'      AND ttext = tsqlt AND tint2 = -32768 AND tint4 = -32768      AND tint8 = -32768 AND treal = -32768"));
    CHECK(1 == conn.get_count("nodes", "tsqlt = '-32767'      AND ttext = tsqlt AND tint2 = -32767 AND tint4 = -32767      AND tint8 = -32767 AND treal = -32767"));
    CHECK(1 == conn.get_count("nodes", "tsqlt = '-2'          AND ttext = tsqlt AND tint2 = -2     AND tint4 = -2          AND tint8 = -2     AND treal =   -2  "));
    CHECK(1 == conn.get_count("nodes", "tsqlt = '-1'          AND ttext = tsqlt AND tint2 = -1     AND tint4 = -1          AND tint8 = -1     AND treal =   -1  "));
    CHECK(1 == conn.get_count("nodes", "tsqlt = '0'           AND ttext = tsqlt AND tint2 =  0     AND tint4 =  0          AND tint8 =  0     AND treal =    0  "));
    CHECK(1 == conn.get_count("nodes", "tsqlt = '1'           AND ttext = tsqlt AND tint2 =  1     AND tint4 =  1          AND tint8 =  1     AND treal =    1  "));
    CHECK(1 == conn.get_count("nodes", "tsqlt = '2'           AND ttext = tsqlt AND tint2 =  2     AND tint4 =  2          AND tint8 =  2     AND treal =    2  "));
    CHECK(1 == conn.get_count("nodes", "tsqlt = '32767'       AND ttext = tsqlt AND tint2 = 32767  AND tint4 = 32767       AND tint8 = 32767  AND treal = 32767 "));
    CHECK(1 == conn.get_count("nodes", "tsqlt = '32768'       AND ttext = tsqlt AND tint2 IS NULL  AND tint4 = 32768       AND tint8 = 32768  AND treal = 32768 "));
    CHECK(1 == conn.get_count("nodes", "tsqlt = '32769'       AND ttext = tsqlt AND tint2 IS NULL  AND tint4 = 32769       AND tint8 = 32769  AND treal = 32769 "));
    CHECK(1 == conn.get_count("nodes", "tsqlt = '2147483647'  AND ttext = tsqlt AND tint2 IS NULL  AND tint4 = 2147483647  AND tint8 = 2147483647               "));
    CHECK(1 == conn.get_count("nodes", "tsqlt = '2147483648'  AND ttext = tsqlt AND tint2 IS NULL  AND tint4 IS NULL       AND tint8 = 2147483648               "));
    CHECK(1 == conn.get_count("nodes", "tsqlt = '2147483649'  AND ttext = tsqlt AND tint2 IS NULL  AND tint4 IS NULL       AND tint8 = 2147483649               "));
    CHECK(1 == conn.get_count("nodes", "tsqlt = ' 42'         AND ttext = tsqlt AND tint2 = 42     AND tint4 = 42          AND tint8 = 42     AND treal =   42  "));
    // clang-format on
}

TEST_CASE("type string (with invalid number)")
{
    testing::opt_t const options = testing::opt_t().flex(conf_file);

    REQUIRE_NOTHROW(db.run_import(
        options, "n10 v1 dV x10.0 y10.0 Ttype=string-with-invalid-number\n"));

    auto conn = db.db().connect();

    CHECK(7 == conn.get_count("nodes"));

    // clang-format off
    CHECK(1 == conn.get_count("nodes", "ttext = ''     AND tint2 IS NULL AND tint4 IS NULL AND tint8 IS NULL"));
    CHECK(1 == conn.get_count("nodes", "ttext = 'abc'  AND tint2 IS NULL AND tint4 IS NULL AND tint8 IS NULL"));
    CHECK(1 == conn.get_count("nodes", "ttext = '0a'   AND tint2 IS NULL AND tint4 IS NULL AND tint8 IS NULL"));
    CHECK(1 == conn.get_count("nodes", "ttext = '0xa'  AND tint2 IS NULL AND tint4 IS NULL AND tint8 IS NULL"));
    CHECK(1 == conn.get_count("nodes", "ttext = '--1'  AND tint2 IS NULL AND tint4 IS NULL AND tint8 IS NULL"));
    CHECK(1 == conn.get_count("nodes", "ttext = '1foo' AND tint2 IS NULL AND tint4 IS NULL AND tint8 IS NULL"));
    CHECK(1 == conn.get_count("nodes", "ttext = '1.2'  AND tint2 IS NULL AND tint4 IS NULL AND tint8 IS NULL"));
    // clang-format on
}

TEST_CASE("type number in column where it doesn't belong")
{
    testing::opt_t const options = testing::opt_t().flex(conf_file);

    REQUIRE_THROWS(db.run_import(
        options, "n10 v1 dV x10.0 y10.0 Ttype=number-fail column=thstr\n"));

    auto conn = db.db().connect();

    CHECK(0 == conn.get_count("nodes"));
}

TEST_CASE("Adding a function should always fail")
{
    testing::opt_t const options = testing::opt_t().flex(conf_file);

    std::string const types[] = {"ttext", "tbool", "tint2", "tint4", "tint8",
                                 "treal", "thstr", "tdirn", "tsqlt"};

    for (auto const &type : types) {
        auto const line =
            "n10 v1 dV x10.0 y10.0 Ttype=function-fail column=" + type + "\n";
        REQUIRE_THROWS(db.run_import(options, line.c_str()));
    }

    auto conn = db.db().connect();

    CHECK(0 == conn.get_count("nodes"));
}

TEST_CASE("type table")
{
    testing::opt_t const options = testing::opt_t().flex(conf_file);

    REQUIRE_NOTHROW(
        db.run_import(options, "n10 v1 dV x10.0 y10.0 Ttype=table\n"));

    auto conn = db.db().connect();

    CHECK(2 == conn.get_count("nodes"));

    CHECK(1 == conn.get_count("nodes", "thstr = ''"));
    CHECK(1 == conn.get_count("nodes", "thstr = 'a=>b,c=>d'"));
}

TEST_CASE("Adding a table with non-strings should fail for hstore")
{
    testing::opt_t const options = testing::opt_t().flex(conf_file);

    char const *const line = "n10 v1 dV x10.0 y10.0 Ttype=table-hstore-fail\n";
    REQUIRE_THROWS(db.run_import(options, line));

    auto conn = db.db().connect();

    CHECK(0 == conn.get_count("nodes"));
}

TEST_CASE("Adding a table should fail except for hstore")
{
    testing::opt_t const options = testing::opt_t().flex(conf_file);

    std::string const types[] = {"ttext", "tbool", "tint2", "tint4",
                                 "tint8", "treal", "tdirn", "tsqlt"};

    for (auto const &type : types) {
        auto const line =
            "n10 v1 dV x10.0 y10.0 Ttype=table-fail column=" + type + "\n";
        REQUIRE_THROWS(db.run_import(options, line.c_str()));
    }

    auto conn = db.db().connect();

    CHECK(0 == conn.get_count("nodes"));
}
