/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <catch.hpp>

#include "properties.hpp"

#include "common-pg.hpp"

TEST_CASE("Store and retrieve properties (memory only)")
{
    properties_t properties{"", ""};

    properties.set_string("foo", "firstvalue");
    properties.set_string("foo", "bar"); // overwriting is okay
    properties.set_string("number", "astring");
    properties.set_int("number", 123); // overwriting with other type okay
    properties.set_bool("decide", true);
    properties.set_string("empty", ""); // empty string is okay

    REQUIRE(properties.get_string("foo", "baz") == "bar");
    REQUIRE(properties.get_string("something", "baz") == "baz");
    REQUIRE(properties.get_string("empty", "baz") == "");
    REQUIRE_THROWS(properties.get_int("foo", 1));
    REQUIRE_THROWS(properties.get_bool("foo", true));

    REQUIRE(properties.get_int("number", 42) == 123);
    REQUIRE(properties.get_int("anumber", 42) == 42);
    REQUIRE(properties.get_string("number", "x") == "123");
    REQUIRE_THROWS(properties.get_bool("number", true));

    REQUIRE(properties.get_bool("decide", false));
    REQUIRE(properties.get_bool("unknown", true));
    REQUIRE_FALSE(properties.get_bool("unknown", false));
    REQUIRE(properties.get_string("decide", "x") == "true");
    REQUIRE_THROWS(properties.get_int("decide", 123));
}

TEST_CASE("Store and retrieve properties (with database)")
{
    for (std::string const schema : {"", "middleschema"}) {
        testing::pg::tempdb_t db;
        auto conn = db.connect();
        if (!schema.empty()) {
            conn.exec("CREATE SCHEMA IF NOT EXISTS {};", schema);
        }

        {
            properties_t properties{db.conninfo(), schema};

            properties.set_string("foo", "bar");
            properties.set_string("empty", "");
            properties.set_int("number", 123);
            properties.set_bool("decide", true);

            properties.store();
        }

        {
            init_database_capabilities(conn);
            std::string const full_table_name =
                (schema.empty() ? "" : schema + ".") + "osm2pgsql_properties";

            REQUIRE(conn.get_count(full_table_name.c_str()) == 4);
            REQUIRE(conn.get_count(full_table_name.c_str(),
                                   "property='foo' AND value='bar'") == 1);
            REQUIRE(conn.get_count(full_table_name.c_str(),
                                   "property='empty' AND value=''") == 1);
            REQUIRE(conn.get_count(full_table_name.c_str(),
                                   "property='number' AND value='123'") == 1);
            REQUIRE(conn.get_count(full_table_name.c_str(),
                                   "property='decide' AND value='true'") == 1);

            properties_t properties{db.conninfo(), schema};
            REQUIRE(properties.load());

            REQUIRE(properties.get_string("foo", "baz") == "bar");
            REQUIRE(properties.get_string("something", "baz") == "baz");
            REQUIRE(properties.get_string("empty", "baz") == "");
            REQUIRE_THROWS(properties.get_int("foo", 1));
            REQUIRE_THROWS(properties.get_bool("foo", true));

            REQUIRE(properties.get_int("number", 42) == 123);
            REQUIRE(properties.get_int("anumber", 42) == 42);
            REQUIRE(properties.get_string("number", "x") == "123");
            REQUIRE_THROWS(properties.get_bool("number", true));

            REQUIRE(properties.get_bool("decide", false));
            REQUIRE(properties.get_bool("unknown", true));
            REQUIRE_FALSE(properties.get_bool("unknown", false));
            REQUIRE(properties.get_string("decide", "x") == "true");
            REQUIRE_THROWS(properties.get_int("decide", 123));
        }
    }
}

TEST_CASE("Update existing properties in database")
{
    testing::pg::tempdb_t db;
    auto conn = db.connect();

    {
        properties_t properties{db.conninfo(), ""};

        properties.set_string("a", "xxx");
        properties.set_string("b", "yyy");

        properties.store();
    }

    {
        init_database_capabilities(conn);
        REQUIRE(conn.get_count("osm2pgsql_properties") == 2);

        properties_t properties{db.conninfo(), ""};
        REQUIRE(properties.load());

        REQUIRE(properties.get_string("a", "def") == "xxx");
        REQUIRE(properties.get_string("b", "def") == "yyy");

        properties.set_string("a", "zzz", false);
        properties.set_string("b", "zzz", true);

        // both are updated in memory
        REQUIRE(properties.get_string("a", "def") == "zzz");
        REQUIRE(properties.get_string("b", "def") == "zzz");
    }

    {
        REQUIRE(conn.get_count("osm2pgsql_properties") == 2);

        properties_t properties{db.conninfo(), ""};
        REQUIRE(properties.load());

        // only "b" was updated in the database
        REQUIRE(properties.get_string("a", "def") == "xxx");
        REQUIRE(properties.get_string("b", "def") == "zzz");
    }
}

TEST_CASE("Load returns false if there are no properties in database")
{
    testing::pg::tempdb_t db;
    auto conn = db.connect();
    init_database_capabilities(conn);

    properties_t properties{db.conninfo(), ""};
    REQUIRE_FALSE(properties.load());
}
