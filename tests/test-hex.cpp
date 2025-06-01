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
