/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2026 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <catch.hpp>

#include <cmath>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "common-pg.hpp"
#include "db-copy-mgr.hpp"
#include "geom.hpp"
#include "wkb.hpp"

namespace {

testing::pg::tempdb_t db;

using copy_mgr_t = db_copy_mgr_t<db_deleter_by_id_t>;

std::shared_ptr<db_target_descr_t> setup_table(std::string const &cols)
{
    auto const conn = db.connect();
    conn.exec("DROP TABLE IF EXISTS test_copy_mgr");
    conn.exec("CREATE TABLE test_copy_mgr (id int8{}{})",
              cols.empty() ? "" : ",", cols);

    auto table =
        std::make_shared<db_target_descr_t>("public", "test_copy_mgr", "id");

    return table;
}

template <typename... ARGS>
void add_row(copy_mgr_t *mgr, std::shared_ptr<db_target_descr_t> const &t,
             ARGS &&...args)
{
    mgr->new_line(t);
    mgr->add_columns(std::forward<ARGS>(args)...);
    mgr->finish_line();

    mgr->sync();
}

void add_array(copy_mgr_t *mgr, std::shared_ptr<db_target_descr_t> const &t,
               int id, std::vector<int> const &values)
{
    mgr->new_line(t);
    mgr->add_column(id);
    mgr->new_array();
    for (auto const &v : values) {
        mgr->add_array_elem(v);
    }
    mgr->finish_array();
    mgr->finish_line();

    mgr->sync();
}

void add_hash(copy_mgr_t *mgr, std::shared_ptr<db_target_descr_t> const &t,
              int id,
              std::vector<std::pair<std::string, std::string>> const &values)
{
    mgr->new_line(t);

    mgr->add_column(id);
    mgr->new_hash();
    for (auto const &[k, v] : values) {
        mgr->add_hash_elem(k, v);
    }
    mgr->finish_hash();
    mgr->finish_line();

    mgr->sync();
}

void check_row(std::vector<std::string> const &row)
{
    auto const conn = db.connect();
    auto const res = conn.require_row("SELECT * FROM test_copy_mgr");

    for (std::size_t i = 0; i < row.size(); ++i) {
        CHECK(res.get_value(0, (int)i) == row[i]);
    }
}

/**
 * Write a row with the value added by the write function into a table with
 * the text format and into one with the binary format, then check that the
 * database ends up with the same value in both.
 */
template <typename FUNC>
void check_text_and_binary(std::string const &sql_type, copy_field_type type,
                           FUNC const &write)
{
    auto const conn = db.connect();
    conn.exec("DROP TABLE IF EXISTS test_copy_text, test_copy_binary");
    conn.exec("CREATE TABLE test_copy_text (id int8, v {})", sql_type);
    conn.exec("CREATE TABLE test_copy_binary (id int8, v {})", sql_type);

    auto const text =
        std::make_shared<db_target_descr_t>("public", "test_copy_text", "id");
    auto const binary =
        std::make_shared<db_target_descr_t>("public", "test_copy_binary", "id");
    binary->set_binary_types({copy_field_type::int8, type});

    copy_mgr_t mgr{std::make_shared<db_copy_thread_t>(db.connection_params())};
    for (auto const &target : {text, binary}) {
        mgr.new_line(target);
        mgr.add_column(1);
        write(&mgr);
        mgr.finish_line();
    }
    mgr.sync();

    auto const res =
        conn.exec("SELECT t.v::text, b.v::text, t.v IS NULL, b.v IS NULL"
                  " FROM test_copy_text t, test_copy_binary b");
    REQUIRE(res.num_tuples() == 1);
    CHECK(std::string{res.get_value(0, 0)} == res.get_value(0, 1));
    CHECK(std::string{res.get_value(0, 2)} == res.get_value(0, 3));
}

template <typename T>
void check_value(std::string const &sql_type, copy_field_type type, T value)
{
    check_text_and_binary(sql_type, type,
                          [&](copy_mgr_t *mgr) { mgr->add_column(value); });
    check_text_and_binary(sql_type, type,
                          [&](copy_mgr_t *mgr) { mgr->add_null_column(); });
}

/// Write a single value in binary format, the write is expected to throw.
template <typename T>
void check_binary_throws(copy_field_type type, T value)
{
    auto const t = setup_table("v text");
    t->set_binary_types({copy_field_type::int8, type});

    copy_mgr_t mgr{std::make_shared<db_copy_thread_t>(db.connection_params())};
    mgr.new_line(t);
    mgr.add_column(1);
    REQUIRE_THROWS(mgr.add_column(value));
    mgr.rollback_line();
    mgr.sync();
}

} // anonymous namespace

TEST_CASE("copy_mgr_t: Insert null")
{
    copy_mgr_t mgr{std::make_shared<db_copy_thread_t>(db.connection_params())};

    auto const t = setup_table("big int8, t text");

    mgr.new_line(t);
    mgr.add_column(0);
    mgr.add_null_column();
    mgr.add_null_column();
    mgr.finish_line();
    mgr.sync();

    auto const conn = db.connect();
    auto const res = conn.require_row("SELECT * FROM test_copy_mgr");

    CHECK(res.is_null(0, 1));
    CHECK(res.is_null(0, 2));
}

TEST_CASE("copy_mgr_t: Insert numbers")
{
    copy_mgr_t mgr{std::make_shared<db_copy_thread_t>(db.connection_params())};

    auto const t = setup_table("big int8, small smallint");

    add_row(&mgr, t, 34, 0xfff12345678ULL, -4457);
    check_row({"34", "17588196497016", "-4457"});
}

TEST_CASE("copy_mgr_t: Insert strings")
{
    copy_mgr_t mgr{std::make_shared<db_copy_thread_t>(db.connection_params())};

    auto const t = setup_table("s0 text, s1 varchar");

    SECTION("Simple strings")
    {
        add_row(&mgr, t, -2, "foo", "l");
        check_row({"-2", "foo", "l"});
    }

    SECTION("Strings with special characters")
    {
        add_row(&mgr, t, -2, "va\tr", "meme\n");
        check_row({"-2", "va\tr", "meme\n"});
    }

    SECTION("Strings with more special characters")
    {
        add_row(&mgr, t, -2, "\rrun", "K\\P");
        check_row({"-2", "\rrun", "K\\P"});
    }

    SECTION("Strings with space and quote")
    {
        add_row(&mgr, t, 1, "with space", "name \"quoted\"");
        check_row({"1", "with space", "name \"quoted\""});
    }
}

TEST_CASE("copy_mgr_t: Insert int arrays")
{
    copy_mgr_t mgr{std::make_shared<db_copy_thread_t>(db.connection_params())};

    auto const t = setup_table("a int[]");

    add_array(&mgr, t, -9000, {45, -2, 0, 56});
    check_row({"-9000", "{45,-2,0,56}"});
}

TEST_CASE("copy_mgr_t: Insert hashes")
{
    copy_mgr_t mgr{std::make_shared<db_copy_thread_t>(db.connection_params())};

    auto const t = setup_table("h hstore");

    std::vector<std::pair<std::string, std::string>> const values = {
        {"one", "two"},           {"key 1", "value 1"},
        {"\"key\"", "\"value\""}, {"key\t2", "value\t2"},
        {"key\n3", "value\n3"},   {"key\r4", "value\r4"},
        {"key\\5", "value\\5"}};

    add_hash(&mgr, t, 42, values);

    auto const c = db.connect();

    for (auto const &[k, v] : values) {
        auto const res = c.result_as_string(
            fmt::format("SELECT h->'{}' FROM test_copy_mgr", k));
        CHECK(res == v);
    }
}

TEST_CASE("copy_mgr_t: Insert something and roll back")
{
    copy_mgr_t mgr{std::make_shared<db_copy_thread_t>(db.connection_params())};

    auto const t = setup_table("t text");

    mgr.new_line(t);
    mgr.add_column(0);
    mgr.add_column("foo");
    mgr.rollback_line();
    mgr.sync();

    auto const conn = db.connect();
    CHECK(conn.get_count("test_copy_mgr") == 0);
}

TEST_CASE("copy_mgr_t: Insert something, insert more, roll back, insert "
          "something else")
{
    copy_mgr_t mgr{std::make_shared<db_copy_thread_t>(db.connection_params())};

    auto const t = setup_table("t text");

    mgr.new_line(t);
    mgr.add_column(0);
    mgr.add_column("good");
    mgr.finish_line();

    mgr.new_line(t);
    mgr.add_column(1);
    mgr.add_column("bad");
    mgr.rollback_line();

    mgr.new_line(t);
    mgr.add_column(2);
    mgr.add_column("better");
    mgr.finish_line();
    mgr.sync();

    auto const conn = db.connect();
    auto const res = conn.exec("SELECT t FROM test_copy_mgr ORDER BY id");
    CHECK(res.num_tuples() == 2);
    CHECK(res.get(0, 0) == "good");
    CHECK(res.get(1, 0) == "better");
}

TEST_CASE("copy_mgr_t: Binary format gives the same text values as text")
{
    for (char const *str : {"foo", "", "va\tr\nK\\P\r\"quoted\"",
                            "\xc3\xa4\xe6\xbc\xa2\xf0\x9f\x99\x82"}) {
        check_value("text", copy_field_type::text, str);
        check_value("text", copy_field_type::text, std::string{str});
    }
    check_value("char(1)", copy_field_type::text, 'N');
    check_value("text", copy_field_type::text, 42);
    check_value("json", copy_field_type::text, R"({"a": [1, 2.5]})");
}

TEST_CASE("copy_mgr_t: Binary format gives the same booleans as text")
{
    check_value("boolean", copy_field_type::boolean, true);
    check_value("boolean", copy_field_type::boolean, false);
    check_value("boolean", copy_field_type::boolean, 1);
}

TEST_CASE("copy_mgr_t: Binary format gives the same integers as text")
{
    for (int64_t const v :
         {int64_t{0}, int64_t{-1}, int64_t{-32768}, int64_t{32767}}) {
        check_value("int2", copy_field_type::int2, v);
    }
    for (int64_t const v : {int64_t{std::numeric_limits<int32_t>::min()},
                            int64_t{std::numeric_limits<int32_t>::max()}}) {
        check_value("int4", copy_field_type::int4, v);
    }
    for (int64_t const v : {std::numeric_limits<int64_t>::min(),
                            std::numeric_limits<int64_t>::max()}) {
        check_value("int8", copy_field_type::int8, v);
    }
    check_value("int4", copy_field_type::int4, -1);
}

TEST_CASE("copy_mgr_t: Binary format gives the same reals as text")
{
    for (double const v :
         {0.0, -0.0, 0.1, 1.5, -123456.789, 3.4028235e38, 1e-40, 1e-45,
          std::numeric_limits<double>::quiet_NaN(),
          -std::numeric_limits<double>::quiet_NaN(),
          std::numeric_limits<double>::infinity(),
          -std::numeric_limits<double>::infinity()}) {
        check_value("real", copy_field_type::float4, v);
        check_value("double precision", copy_field_type::float8, v);
    }
    check_value("double precision", copy_field_type::float8, 1e300);
    check_value("real", copy_field_type::float4, 7);
}

TEST_CASE("copy_mgr_t: Binary format rejects reals out of range like text")
{
    check_binary_throws(copy_field_type::float4, 1e39);
    check_binary_throws(copy_field_type::float4, -1e39);
    check_binary_throws(copy_field_type::float4, 1e-50);
}

TEST_CASE("copy_mgr_t: Binary format gives the same jsonb as text")
{
    check_value("jsonb", copy_field_type::jsonb,
                R"({"b": null, "a": [1, 2.5, "x\ty"], "c": "ä"})");
    check_value("jsonb", copy_field_type::jsonb, std::string{"[]"});
}

TEST_CASE("copy_mgr_t: Binary format rounds doubles to the nearest float")
{
    // The text format rounds twice (to the shortest decimal representation,
    // then to float) which gives a different result for values exactly
    // halfway between two floats. The binary format rounds once, correctly.
    auto const t = setup_table("v real");
    t->set_binary_types({copy_field_type::int8, copy_field_type::float4});

    copy_mgr_t mgr{std::make_shared<db_copy_thread_t>(db.connection_params())};
    double const halfway_down = 1.0 + std::ldexp(1.0, -24);   // to even: 1
    double const halfway_up = 1.0 + 3 * std::ldexp(1.0, -24); // 1 + 2^-22
    add_row(&mgr, t, 1, halfway_down);
    add_row(&mgr, t, 2, halfway_up);

    auto const conn = db.connect();
    CHECK(conn.result_as_int("SELECT count(*) FROM test_copy_mgr WHERE"
                             " (id = 1 AND v = 1::real) OR"
                             " (id = 2 AND v = (1 + 2 ^ (-22))::real)") == 2);
}

TEST_CASE("copy_mgr_t: Binary format gives the same timestamps as text")
{
    for (uint32_t const seconds : {1U, 946684800U, 1234567890U, 0xffffffffU}) {
        check_value("timestamptz", copy_field_type::timestamptz,
                    osmium::Timestamp{seconds});
    }
    check_binary_throws(copy_field_type::timestamptz, osmium::Timestamp{});
}

TEST_CASE("copy_mgr_t: Binary format gives the same arrays as text")
{
    for (std::vector<osmid_t> const &values :
         {std::vector<osmid_t>{}, std::vector<osmid_t>{1, -2, 3},
          std::vector<osmid_t>{std::numeric_limits<osmid_t>::max()}}) {
        check_text_and_binary("int8[]", copy_field_type::int8_array,
                              [&](copy_mgr_t *mgr) {
                                  mgr->new_array();
                                  for (auto const v : values) {
                                      mgr->add_array_elem(v);
                                  }
                                  mgr->finish_array();
                              });
    }
}

TEST_CASE("copy_mgr_t: Binary format gives the same hstore as text")
{
    std::vector<std::pair<std::string, std::string>> const values = {
        {"one", "two"},
        {"key 1", "value 1"},
        {"\"key\"", "\"value\""},
        {"key\t2", "value\t2"},
        {"key\n3", "value\n3"},
        {"key\\5", "value\\5"},
        {"", ""}};

    check_text_and_binary("hstore", copy_field_type::hstore,
                          [&](copy_mgr_t *mgr) {
                              mgr->new_hash();
                              for (auto const &[k, v] : values) {
                                  mgr->add_hash_elem(k, v);
                              }
                              mgr->add_hstore_num_noescape("num", 17);
                              mgr->finish_hash();
                          });

    check_text_and_binary("hstore", copy_field_type::hstore,
                          [&](copy_mgr_t *mgr) {
                              mgr->new_hash();
                              mgr->finish_hash();
                          });
}

TEST_CASE("copy_mgr_t: Binary format gives the same geometries as text")
{
    geom::geometry_t point{geom::point_t{1.5, -2.25}};
    point.set_srid(3857);
    geom::geometry_t line{geom::linestring_t{{0, 0}, {1, 1}, {2, 0}}};
    line.set_srid(4326);

    for (auto const *geom : {&point, &line}) {
        for (bool const wrap_multi : {false, true}) {
            auto const wkb = geom_to_ewkb(*geom, wrap_multi);
            check_text_and_binary(
                "geometry", copy_field_type::geometry,
                [&](copy_mgr_t *mgr) { mgr->add_hex_geom(wkb); });
        }
    }
}

TEST_CASE("copy_mgr_t: Binary format with deletes, rollback and many rows")
{
    auto const t = setup_table("t text, n int4");
    t->set_binary_types(
        {copy_field_type::int8, copy_field_type::text, copy_field_type::int4});

    copy_mgr_t mgr{std::make_shared<db_copy_thread_t>(db.connection_params())};

    // Long enough so that the rows need several buffers
    std::string const text(200, 'x');

    // A new_line() just for deleting must not leave anything in the buffer.
    mgr.new_line(t);
    mgr.delete_object(1);

    for (int i = 0; i < 100000; ++i) {
        mgr.new_line(t);
        mgr.add_column(i);
        mgr.add_column(text);
        if (i % 3 == 0) {
            mgr.rollback_line();
            continue;
        }
        mgr.add_column(i * 2);
        mgr.finish_line();
    }
    mgr.sync();

    auto const conn = db.connect();
    CHECK(conn.get_count("test_copy_mgr") == 66666);
    CHECK(conn.result_as_int("SELECT count(*) FROM test_copy_mgr"
                             " WHERE n = id * 2 AND t = repeat('x', 200)") ==
          66666);
}
