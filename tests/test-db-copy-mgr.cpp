#include <string>
#include <utility>
#include <vector>

#include <catch.hpp>

#include "common-pg.hpp"
#include "db-copy-mgr.hpp"

static pg::tempdb_t db;

using copy_mgr_t = db_copy_mgr_t<db_deleter_by_id_t>;

static std::shared_ptr<db_target_descr_t> setup_table(std::string const &cols)
{
    auto conn = db.connect();
    conn.exec("DROP TABLE IF EXISTS test_copy_mgr");
    conn.exec("CREATE TABLE test_copy_mgr (id int8{}{})"_format(
        cols.empty() ? "" : ",", cols));

    auto table = std::make_shared<db_target_descr_t>();
    table->name = "test_copy_mgr";
    table->id = "id";

    return table;
}

template <typename... ARGS>
void add_row(copy_mgr_t &mgr, std::shared_ptr<db_target_descr_t> const &t,
             ARGS &&... args)
{
    mgr.new_line(t);
    mgr.add_columns(std::forward<ARGS>(args)...);
    mgr.finish_line();

    mgr.sync();
}

template <typename T>
void add_array(copy_mgr_t &mgr, std::shared_ptr<db_target_descr_t> const &t,
               int id, std::vector<T> const &values)
{
    mgr.new_line(t);
    mgr.add_column(id);
    mgr.new_array();
    for (auto const &v : values) {
        mgr.add_array_elem(v);
    }
    mgr.finish_array();
    mgr.finish_line();

    mgr.sync();
}

static void
add_hash(copy_mgr_t &mgr, std::shared_ptr<db_target_descr_t> const &t, int id,
         std::vector<std::pair<std::string, std::string>> const &values)
{
    mgr.new_line(t);

    mgr.add_column(id);
    mgr.new_hash();
    for (auto const &v : values) {
        mgr.add_hash_elem(v.first, v.second);
    }
    mgr.finish_hash();
    mgr.finish_line();

    mgr.sync();
}

static void check_row(std::vector<std::string> const &row)
{
    auto conn = db.connect();
    auto res = conn.require_row("SELECT * FROM test_copy_mgr");

    for (std::size_t i = 0; i < row.size(); ++i) {
        CHECK(res.get_value(0, (int)i) == row[i]);
    }
}

TEST_CASE("copy_mgr_t")
{
    copy_mgr_t mgr(std::make_shared<db_copy_thread_t>(db.conninfo()));

    SECTION("Insert null")
    {
        auto t = setup_table("big int8, t text");

        mgr.new_line(t);
        mgr.add_column(0);
        mgr.add_null_column();
        mgr.add_null_column();
        mgr.finish_line();
        mgr.sync();

        auto conn = db.connect();
        auto res = conn.require_row("SELECT * FROM test_copy_mgr");

        CHECK(res.is_null(0, 1));
        CHECK(res.is_null(0, 2));
    }

    SECTION("Insert numbers")
    {
        auto t = setup_table("big int8, small smallint");

        add_row(mgr, t, 34, 0xfff12345678ULL, -4457);
        check_row({"34", "17588196497016", "-4457"});
    }

    SECTION("Insert strings")
    {
        auto t = setup_table("s0 text, s1 varchar");

        SECTION("Simple strings")
        {
            add_row(mgr, t, -2, "foo", "l");
            check_row({"-2", "foo", "l"});
        }

        SECTION("Strings with special characters")
        {
            add_row(mgr, t, -2, "va\tr", "meme\n");
            check_row({"-2", "va\tr", "meme\n"});
        }

        SECTION("Strings with more special characters")
        {
            add_row(mgr, t, -2, "\rrun", "K\\P");
            check_row({"-2", "\rrun", "K\\P"});
        }

        SECTION("Strings with space and quote")
        {
            add_row(mgr, t, 1, "with space", "name \"quoted\"");
            check_row({"1", "with space", "name \"quoted\""});
        }
    }

    SECTION("Insert int arrays")
    {
        auto t = setup_table("a int[]");

        add_array<int>(mgr, t, -9000, {45, -2, 0, 56});
        check_row({"-9000", "{45,-2,0,56}"});
    }

    SECTION("Insert string arrays")
    {
        auto t = setup_table("a text[]");

        add_array<std::string>(mgr, t, 3,
                               {"foo", "", "with space", "with \"quote\"",
                                "the\t", "line\nbreak", "rr\rrr", "s\\l"});
        check_row({"3", "{foo,\"\",\"with space\",\"with "
                        "\\\"quote\\\"\",\"the\t\",\"line\nbreak\","
                        "\"rr\rrr\",\"s\\\\l\"}"});

        auto c = db.connect();
        CHECK(c.require_scalar<std::string>("SELECT a[4] FROM test_copy_mgr") ==
              "with \"quote\"");
        CHECK(c.require_scalar<std::string>("SELECT a[5] FROM test_copy_mgr") ==
              "the\t");
        CHECK(c.require_scalar<std::string>("SELECT a[6] FROM test_copy_mgr") ==
              "line\nbreak");
        CHECK(c.require_scalar<std::string>("SELECT a[7] FROM test_copy_mgr") ==
              "rr\rrr");
        CHECK(c.require_scalar<std::string>("SELECT a[8] FROM test_copy_mgr") ==
              "s\\l");
    }

    SECTION("Insert hashes")
    {
        auto t = setup_table("h hstore");

        std::vector<std::pair<std::string, std::string>> const values = {
            {"one", "two"},           {"key 1", "value 1"},
            {"\"key\"", "\"value\""}, {"key\t2", "value\t2"},
            {"key\n3", "value\n3"},   {"key\r4", "value\r4"},
            {"key\\5", "value\\5"}};

        add_hash(mgr, t, 42, values);

        auto c = db.connect();

        for (auto const &v : values) {
            auto const res = c.require_scalar<std::string>(
                "SELECT h->'{}' FROM test_copy_mgr"_format(v.first));
            CHECK(res == v.second);
        }
    }
}
