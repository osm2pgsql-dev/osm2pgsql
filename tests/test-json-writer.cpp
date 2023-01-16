/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <catch.hpp>

#include "json-writer.hpp"

TEST_CASE("json writer writes null", "[NoDB]")
{
    json_writer_t writer;
    writer.null();
    REQUIRE(writer.json() == "null");
}

TEST_CASE("json writer writes true", "[NoDB]")
{
    json_writer_t writer;
    writer.boolean(true);
    REQUIRE(writer.json() == "true");
}

TEST_CASE("json writer writes false", "[NoDB]")
{
    json_writer_t writer;
    writer.boolean(false);
    REQUIRE(writer.json() == "false");
}

TEST_CASE("json writer writes integer", "[NoDB]")
{
    json_writer_t writer;
    writer.number(17);
    REQUIRE(writer.json() == "17");
}

TEST_CASE("json writer writes real number", "[NoDB]")
{
    json_writer_t writer;
    writer.number(3.141);
    REQUIRE(writer.json() == "3.141");
}

TEST_CASE("json writer writes invalid real number as null", "[NoDB]")
{
    json_writer_t writer;
    writer.number(INFINITY);
    REQUIRE(writer.json() == "null");
}

TEST_CASE("json writer writes NaN as null", "[NoDB]")
{
    json_writer_t writer;
    writer.number(NAN);
    REQUIRE(writer.json() == "null");
}

TEST_CASE("json writer writes string", "[NoDB]")
{
    json_writer_t writer;
    writer.string("foo");
    REQUIRE(writer.json() == "\"foo\"");
}

TEST_CASE("json writer writes empty array", "[NoDB]")
{
    json_writer_t writer;
    writer.start_array();
    writer.end_array();
    REQUIRE(writer.json() == "[]");
}

TEST_CASE("json writer writes array with one thing", "[NoDB]")
{
    json_writer_t writer;
    writer.start_array();
    writer.string("foo");
    writer.end_array();
    REQUIRE(writer.json() == "[\"foo\"]");
}

TEST_CASE("json writer writes array with two things", "[NoDB]")
{
    json_writer_t writer;
    writer.start_array();
    writer.string("foo");
    writer.next();
    writer.number(42);
    writer.end_array();
    REQUIRE(writer.json() == "[\"foo\",42]");
}

TEST_CASE("json writer writes array with extra next()", "[NoDB]")
{
    json_writer_t writer;
    writer.start_array();
    writer.string("foo");
    writer.next();
    writer.number(42);
    writer.next();
    writer.end_array();
    REQUIRE(writer.json() == "[\"foo\",42]");
}

TEST_CASE("json writer writes empty object", "[NoDB]")
{
    json_writer_t writer;
    writer.start_object();
    writer.end_object();
    REQUIRE(writer.json() == "{}");
}

TEST_CASE("json writer writes object with one thing", "[NoDB]")
{
    json_writer_t writer;
    writer.start_object();
    writer.key("foo");
    writer.string("bar");
    writer.end_object();
    REQUIRE(writer.json() == "{\"foo\":\"bar\"}");
}

TEST_CASE("json writer writes object with two things", "[NoDB]")
{
    json_writer_t writer;
    writer.start_object();
    writer.key("a");
    writer.string("str");
    writer.next();
    writer.key("b");
    writer.number(42);
    writer.end_object();
    REQUIRE(writer.json() == "{\"a\":\"str\",\"b\":42}");
}

TEST_CASE("json writer writes object with extra next()", "[NoDB]")
{
    json_writer_t writer;
    writer.start_object();
    writer.key("a");
    writer.string("str");
    writer.next();
    writer.key("b");
    writer.number(42);
    writer.next();
    writer.end_object();
    REQUIRE(writer.json() == "{\"a\":\"str\",\"b\":42}");
}

TEST_CASE("json writer with strange chars in string", "[NoDB]")
{
    json_writer_t writer;
    writer.string("abc-\"-\\-\b-\f-\n-\r-\t-abc");
    REQUIRE(writer.json() == "\"abc-\\\"-\\\\-\\b-\\f-\\n-\\r-\\t-abc\"");
}

TEST_CASE("json writer with even stranger chars in string", "[NoDB]")
{
    json_writer_t writer;
    writer.string("abc-\x01-\x1f-abc");
    REQUIRE(writer.json() == "\"abc-\\u0001-\\u001f-abc\"");
}

