/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2025 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <catch.hpp>

#include <string>
#include <utility>
#include <vector>

#include "common-pg.hpp"
#include "db-copy-mgr.hpp"

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
