/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <catch.hpp>

#include "settings.hpp"

#include "common-pg.hpp"

TEST_CASE("Store and retrieve settings (memory only)")
{
    settings_t settings{"", ""};

    settings.set_string("foo", "firstvalue");
    settings.set_string("foo", "bar"); // overwriting is okay
    settings.set_string("number", "astring");
    settings.set_int("number", 123); // overwriting with other type okay
    settings.set_bool("decide", true);
    settings.set_string("empty", ""); // empty string is okay

    REQUIRE(settings.get_string("foo", "baz") == "bar");
    REQUIRE(settings.get_string("something", "baz") == "baz");
    REQUIRE(settings.get_string("empty", "baz") == "");
    REQUIRE_THROWS(settings.get_int("foo", 1));
    REQUIRE_THROWS(settings.get_bool("foo", true));

    REQUIRE(settings.get_int("number", 42) == 123);
    REQUIRE(settings.get_int("anumber", 42) == 42);
    REQUIRE(settings.get_string("number", "x") == "123");
    REQUIRE_THROWS(settings.get_bool("number", true));

    REQUIRE(settings.get_bool("decide", false));
    REQUIRE(settings.get_bool("unknown", true));
    REQUIRE_FALSE(settings.get_bool("unknown", false));
    REQUIRE(settings.get_string("decide", "x") == "true");
    REQUIRE_THROWS(settings.get_int("decide", 123));
}

TEST_CASE("Store and retrieve settings (with database)")
{
    for (std::string const schema : {"", "middleschema"}) {
        testing::pg::tempdb_t db;
        auto conn = db.connect();
        if (!schema.empty()) {
            conn.exec("CREATE SCHEMA IF NOT EXISTS {};", schema);
        }

        {
            settings_t settings{db.conninfo(), schema};

            settings.set_string("foo", "bar");
            settings.set_string("empty", "");
            settings.set_int("number", 123);
            settings.set_bool("decide", true);

            settings.store();
        }

        {
            init_database_capabilities(conn);
            std::string const full_table_name =
                (schema.empty() ? "" : schema + ".") + "osm2pgsql_settings";

            REQUIRE(conn.get_count(full_table_name.c_str()) == 4);
            REQUIRE(conn.get_count(full_table_name.c_str(),
                                   "option='foo' AND value='bar'") == 1);
            REQUIRE(conn.get_count(full_table_name.c_str(),
                                   "option='empty' AND value=''") == 1);
            REQUIRE(conn.get_count(full_table_name.c_str(),
                                   "option='number' AND value='123'") == 1);
            REQUIRE(conn.get_count(full_table_name.c_str(),
                                   "option='decide' AND value='true'") == 1);

            settings_t settings{db.conninfo(), schema};
            REQUIRE(settings.load());

            REQUIRE(settings.get_string("foo", "baz") == "bar");
            REQUIRE(settings.get_string("something", "baz") == "baz");
            REQUIRE(settings.get_string("empty", "baz") == "");
            REQUIRE_THROWS(settings.get_int("foo", 1));
            REQUIRE_THROWS(settings.get_bool("foo", true));

            REQUIRE(settings.get_int("number", 42) == 123);
            REQUIRE(settings.get_int("anumber", 42) == 42);
            REQUIRE(settings.get_string("number", "x") == "123");
            REQUIRE_THROWS(settings.get_bool("number", true));

            REQUIRE(settings.get_bool("decide", false));
            REQUIRE(settings.get_bool("unknown", true));
            REQUIRE_FALSE(settings.get_bool("unknown", false));
            REQUIRE(settings.get_string("decide", "x") == "true");
            REQUIRE_THROWS(settings.get_int("decide", 123));
        }
    }
}

TEST_CASE("Update existing settings in database")
{
    testing::pg::tempdb_t db;
    auto conn = db.connect();

    {
        settings_t settings{db.conninfo(), ""};

        settings.set_string("a", "xxx");
        settings.set_string("b", "yyy");

        settings.store();
    }

    {
        init_database_capabilities(conn);
        REQUIRE(conn.get_count("osm2pgsql_settings") == 2);

        settings_t settings{db.conninfo(), ""};
        REQUIRE(settings.load());

        REQUIRE(settings.get_string("a", "def") == "xxx");
        REQUIRE(settings.get_string("b", "def") == "yyy");

        settings.set_string("a", "zzz", false);
        settings.set_string("b", "zzz", true);

        // both are updated in memory
        REQUIRE(settings.get_string("a", "def") == "zzz");
        REQUIRE(settings.get_string("b", "def") == "zzz");
    }

    {
        REQUIRE(conn.get_count("osm2pgsql_settings") == 2);

        settings_t settings{db.conninfo(), ""};
        REQUIRE(settings.load());

        // only "b" was updated in the database
        REQUIRE(settings.get_string("a", "def") == "xxx");
        REQUIRE(settings.get_string("b", "def") == "zzz");
    }
}

TEST_CASE("Load returns false if there are no settings in database")
{
    testing::pg::tempdb_t db;
    auto conn = db.connect();
    init_database_capabilities(conn);

    settings_t settings{db.conninfo(), ""};
    REQUIRE_FALSE(settings.load());
}
