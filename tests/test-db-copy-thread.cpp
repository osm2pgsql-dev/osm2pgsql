#include <catch.hpp>

#include "common-pg.hpp"
#include "db-copy.hpp"
#include "gazetteer-style.hpp"

static pg::tempdb_t db;

static int table_count(pg::conn_t const &conn, std::string const &where = "")
{
    return conn.require_scalar<int>("SELECT count(*) FROM test_copy_thread " +
                                    where);
}

TEST_CASE("db_copy_thread_t with db_deleter_by_id_t")
{
    auto conn = db.connect();
    conn.exec("DROP TABLE IF EXISTS test_copy_thread");
    conn.exec("CREATE TABLE test_copy_thread (id int8)");

    auto table = std::make_shared<db_target_descr_t>();
    table->name = "test_copy_thread";
    table->id = "id";

    db_copy_thread_t t(db.conninfo());
    using cmd_copy_t = db_cmd_copy_delete_t<db_deleter_by_id_t>;
    auto cmd = std::unique_ptr<cmd_copy_t>(new cmd_copy_t{table});

    SECTION("simple copy command")
    {

        SECTION("add one copy line and sync")
        {
            cmd->buffer += "42\n";

            t.add_buffer(std::unique_ptr<db_cmd_t>(cmd.release()));
            t.sync_and_wait();

            REQUIRE(conn.require_scalar<int>(
                        "SELECT id FROM test_copy_thread") == 42);
        }

        SECTION("add multiple rows and sync")
        {
            cmd->buffer += "101\n  23\n 900\n";

            t.add_buffer(std::unique_ptr<db_cmd_t>(cmd.release()));
            t.sync_and_wait();

            REQUIRE(table_count(conn) == 3);
        }

        SECTION("add one line and finish")
        {
            cmd->buffer += "2\n";

            t.add_buffer(std::unique_ptr<db_cmd_t>(cmd.release()));
            t.finish();

            REQUIRE(conn.require_scalar<int>(
                        "SELECT id FROM test_copy_thread") == 2);
        }
    }

    SECTION("delete command")
    {
        cmd->buffer += "42\n43\n133\n223\n224\n";
        t.add_buffer(std::unique_ptr<db_cmd_t>(cmd.release()));
        t.sync_and_wait();

        cmd = std::unique_ptr<cmd_copy_t>(new cmd_copy_t{table});

        SECTION("simple delete of existing rows")
        {
            cmd->add_deletable(223);
            cmd->add_deletable(42);

            t.add_buffer(std::unique_ptr<db_cmd_t>(cmd.release()));
            t.sync_and_wait();

            REQUIRE(table_count(conn, "WHERE id = 42") == 0);
            REQUIRE(table_count(conn, "WHERE id = 223") == 0);
        }

        SECTION("delete one and add another")
        {
            cmd->add_deletable(133);
            cmd->buffer += "134\n";

            t.add_buffer(std::unique_ptr<db_cmd_t>(cmd.release()));
            t.sync_and_wait();

            REQUIRE(table_count(conn, "WHERE id = 133") == 0);
            REQUIRE(table_count(conn, "WHERE id = 134") == 1);
        }

        SECTION("delete one and add the same")
        {
            cmd->add_deletable(133);
            cmd->buffer += "133\n";

            t.add_buffer(std::unique_ptr<db_cmd_t>(cmd.release()));
            t.sync_and_wait();

            REQUIRE(table_count(conn, "WHERE id = 133") == 1);
        }
    }

    SECTION("multi buffer add without delete")
    {
        cmd->buffer += "542\n5543\n10133\n";
        t.add_buffer(std::unique_ptr<db_cmd_t>(cmd.release()));

        cmd = std::unique_ptr<cmd_copy_t>(new cmd_copy_t{table});
        cmd->buffer += "12\n784\n523\n";
        t.add_buffer(std::unique_ptr<db_cmd_t>(cmd.release()));

        t.finish();

        REQUIRE(table_count(conn) == 6);
        REQUIRE(table_count(conn, "WHERE id = 10133") == 1);
        REQUIRE(table_count(conn, "WHERE id = 523") == 1);
    }

    SECTION("multi buffer add with delete")
    {
        cmd->buffer += "542\n5543\n10133\n";
        t.add_buffer(std::unique_ptr<db_cmd_t>(cmd.release()));

        cmd = std::unique_ptr<cmd_copy_t>(new cmd_copy_t{table});
        cmd->add_deletable(542);
        cmd->buffer += "12\n";
        t.add_buffer(std::unique_ptr<db_cmd_t>(cmd.release()));

        t.finish();

        REQUIRE(table_count(conn) == 3);
        REQUIRE(table_count(conn, "WHERE id = 542") == 0);
        REQUIRE(table_count(conn, "WHERE id = 12") == 1);
    }
}

TEST_CASE("db_copy_thread_t with db_deleter_place_t")
{
    auto conn = db.connect();
    conn.exec("DROP TABLE IF EXISTS test_copy_thread");
    conn.exec("CREATE TABLE test_copy_thread ("
              "osm_type char(1),"
              "osm_id bigint,"
              "class text)");

    auto table = std::make_shared<db_target_descr_t>();
    table->name = "test_copy_thread";
    table->id = "place_id";

    db_copy_thread_t t(db.conninfo());
    using cmd_copy_t = db_cmd_copy_delete_t<db_deleter_place_t>;
    auto cmd = std::unique_ptr<cmd_copy_t>(new cmd_copy_t{table});

    SECTION("simple delete")
    {
        cmd->buffer += "N\t42\tbuilding\n"
                       "N\t43\tbuilding\n"
                       "W\t42\thighway\n"
                       "R\t42\twaterway\n";

        t.add_buffer(std::unique_ptr<db_cmd_t>(cmd.release()));
        t.sync_and_wait();

        cmd = std::unique_ptr<cmd_copy_t>(new cmd_copy_t{table});

        SECTION("full delete of existing rows")
        {
            cmd->add_deletable('N', 42);
            cmd->add_deletable('R', 42);

            t.add_buffer(std::unique_ptr<db_cmd_t>(cmd.release()));
            t.sync_and_wait();

            REQUIRE(table_count(conn, "WHERE osm_type = 'N'"
                                      "      and osm_id = 42") == 0);
            REQUIRE(table_count(conn, "WHERE osm_type = 'N'"
                                      "      and osm_id = 43") == 1);
            REQUIRE(table_count(conn, "WHERE osm_type = 'W'"
                                      "      and osm_id = 42") == 1);
            REQUIRE(table_count(conn, "WHERE osm_type = 'R'"
                                      "      and osm_id = 42") == 0);
        }

        SECTION("partial delete of existing rows")
        {
            cmd->add_deletable('N', 42, "'road','building','amenity'");
            cmd->add_deletable('R', 42, "'road','building','amenity'");

            t.add_buffer(std::unique_ptr<db_cmd_t>(cmd.release()));
            t.sync_and_wait();

            REQUIRE(table_count(conn, "WHERE osm_type = 'N'"
                                      "      and osm_id = 42") == 1);
            REQUIRE(table_count(conn, "WHERE osm_type = 'N'"
                                      "      and osm_id = 43") == 1);
            REQUIRE(table_count(conn, "WHERE osm_type = 'W'"
                                      "      and osm_id = 42") == 1);
            REQUIRE(table_count(conn, "WHERE osm_type = 'R'"
                                      "      and osm_id = 42") == 0);
        }

        SECTION("delete one add another id")
        {
            cmd->add_deletable('R', 42);
            cmd->buffer += "W\t43\tamenity\n";

            t.add_buffer(std::unique_ptr<db_cmd_t>(cmd.release()));
            t.sync_and_wait();

            REQUIRE(table_count(conn, "WHERE osm_type = 'R'"
                                      "      and osm_id = 42") == 0);
            REQUIRE(table_count(conn, "WHERE osm_type = 'W'"
                                      "      and osm_id = 43") == 1);
        }

        SECTION("delete one add another class type")
        {
            cmd->add_deletable('W', 42, "'amenity'");
            cmd->buffer += "W\t42\tamenity\n";

            t.add_buffer(std::unique_ptr<db_cmd_t>(cmd.release()));
            t.sync_and_wait();

            REQUIRE(table_count(conn, "WHERE osm_type = 'W' and osm_id = 42"
                                      "      and class = 'highway'") == 0);
            REQUIRE(table_count(conn, "WHERE osm_type = 'W' and osm_id = 42"
                                      "      and class = 'amenity'") == 1);
        }
    }
}
