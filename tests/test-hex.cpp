/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2025 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <catch.hpp>

#include "hex.hpp"

#include <string>

TEST_CASE("hex encode a string", "[NoDB]")
{
    std::string result;
    util::encode_hex("ab~", &result);
    REQUIRE(result.size() == 6);
    REQUIRE(result == "61627E");
}

TEST_CASE("hex encode a string adding to existing string", "[NoDB]")
{
    std::string result{"0x"};
    util::encode_hex("\xca\xfe", &result);
    REQUIRE(result.size() == 6);
    REQUIRE(result == "0xCAFE");
}

TEST_CASE("hex encoding an empty string doesn't change output string", "[NoDB]")
{
    std::string result{"foo"};
    util::encode_hex("", &result);
    REQUIRE(result == "foo");
}

TEST_CASE("wkb hex decode of valid and invalid hex characters")
{
    REQUIRE(util::decode_hex_char('0') == 0);
    REQUIRE(util::decode_hex_char('9') == 9);
    REQUIRE(util::decode_hex_char('a') == 0x0a);
    REQUIRE(util::decode_hex_char('f') == 0x0f);
    REQUIRE(util::decode_hex_char('A') == 0x0a);
    REQUIRE(util::decode_hex_char('F') == 0x0f);
    REQUIRE(util::decode_hex_char('#') == 0);
    REQUIRE(util::decode_hex_char('@') == 0);
    REQUIRE(util::decode_hex_char('g') == 0);
    REQUIRE(util::decode_hex_char('G') == 0);
    REQUIRE(util::decode_hex_char(0x7f) == 0);
}

TEST_CASE("wkb hex decode of valid hex string")
{
    std::string const hex{"0001020F1099FF"};
    std::string const data = {0x00,
                              0x01,
                              0x02,
                              0x0f,
                              0x10,
                              static_cast<char>(0x99),
                              static_cast<char>(0xff)};

    auto const result = util::decode_hex(hex);
    REQUIRE(result.size() == hex.size() / 2);
    REQUIRE(result == data);
}

TEST_CASE("wkb hex decode of empty string is okay")
{
    std::string const hex{};
    REQUIRE(util::decode_hex(hex).empty());
}

TEST_CASE("wkb hex decode of string with odd number of characters fails")
{
    REQUIRE_THROWS(util::decode_hex("a"));
    REQUIRE_THROWS(util::decode_hex("abc"));
    REQUIRE_THROWS(util::decode_hex("00000"));
}

TEST_CASE("hex encode and decode") {
    std::string const str{"something somewhere"};
    REQUIRE(util::decode_hex(util::encode_hex(str)) == str);
}
