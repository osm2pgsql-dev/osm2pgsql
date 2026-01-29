/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2026 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <catch.hpp>

#include "common-pg.hpp"
#include "db-copy.hpp"

namespace {

testing::pg::tempdb_t db;

int table_count(testing::pg::conn_t const &conn, std::string const &where = "")
{
    return conn.result_as_int("SELECT count(*) FROM test_copy_thread " + where);
}

} // anonymous namespace

TEST_CASE("db_copy_thread_t with db_deleter_by_id_t")
{
    auto const conn = db.connect();
    conn.exec("DROP TABLE IF EXISTS test_copy_thread");
    conn.exec("CREATE TABLE test_copy_thread (id int8)");

    auto const table =
        std::make_shared<db_target_descr_t>("public", "test_copy_thread", "id");

    db_copy_thread_t t{db.connection_params()};
    using cmd_copy_t = db_cmd_copy_delete_t<db_deleter_by_id_t>;

    SECTION("simple copy command")
    {

        SECTION("add one copy line and sync")
        {
            cmd_copy_t cmd{table};
            cmd.buffer += "42\n";

            t.send_command(std::move(cmd));
            t.sync_and_wait();

            REQUIRE(conn.result_as_int("SELECT id FROM test_copy_thread") ==
                    42);
        }

        SECTION("add multiple rows and sync")
        {
            cmd_copy_t cmd{table};
            cmd.buffer += "101\n  23\n 900\n";

            t.send_command(std::move(cmd));
            t.sync_and_wait();

            REQUIRE(table_count(conn) == 3);
        }

        SECTION("add one line and finish")
        {
            cmd_copy_t cmd{table};
            cmd.buffer += "2\n";

            t.send_command(std::move(cmd));
            t.finish();

            REQUIRE(conn.result_as_int("SELECT id FROM test_copy_thread") == 2);
        }
    }

    SECTION("delete command")
    {
        cmd_copy_t cmd{table};
        cmd.buffer += "42\n43\n133\n223\n224\n";
        t.send_command(std::move(cmd));
        t.sync_and_wait();

        SECTION("simple delete of existing rows")
        {
            cmd = cmd_copy_t{table};
            cmd.add_deletable(223);
            cmd.add_deletable(42);

            t.send_command(std::move(cmd));
            t.sync_and_wait();

            REQUIRE(table_count(conn, "WHERE id = 42") == 0);
            REQUIRE(table_count(conn, "WHERE id = 223") == 0);
        }

        SECTION("delete one and add another")
        {
            cmd = cmd_copy_t{table};
            cmd.add_deletable(133);
            cmd.buffer += "134\n";

            t.send_command(std::move(cmd));
            t.sync_and_wait();

            REQUIRE(table_count(conn, "WHERE id = 133") == 0);
            REQUIRE(table_count(conn, "WHERE id = 134") == 1);
        }

        SECTION("delete one and add the same")
        {
            cmd = cmd_copy_t{table};
            cmd.add_deletable(133);
            cmd.buffer += "133\n";

            t.send_command(std::move(cmd));
            t.sync_and_wait();

            REQUIRE(table_count(conn, "WHERE id = 133") == 1);
        }
    }

    SECTION("multi buffer add without delete")
    {
        cmd_copy_t cmd{table};
        cmd.buffer += "542\n5543\n10133\n";
        t.send_command(std::move(cmd));

        cmd = cmd_copy_t{table};
        cmd.buffer += "12\n784\n523\n";
        t.send_command(std::move(cmd));

        t.finish();

        REQUIRE(table_count(conn) == 6);
        REQUIRE(table_count(conn, "WHERE id = 10133") == 1);
        REQUIRE(table_count(conn, "WHERE id = 523") == 1);
    }

    SECTION("multi buffer add with delete")
    {
        cmd_copy_t cmd{table};
        cmd.buffer += "542\n5543\n10133\n";
        t.send_command(std::move(cmd));

        cmd = cmd_copy_t{table};
        cmd.add_deletable(542);
        cmd.buffer += "12\n";
        t.send_command(std::move(cmd));

        t.finish();

        REQUIRE(table_count(conn) == 3);
        REQUIRE(table_count(conn, "WHERE id = 542") == 0);
        REQUIRE(table_count(conn, "WHERE id = 12") == 1);
    }
}
