/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <catch.hpp>

#include "util.hpp"

#include <cstring>
#include <limits>

TEST_CASE("integer_to_buffer 1", "[NoDB]")
{
    util::integer_to_buffer buffer{1};
    REQUIRE(std::strcmp(buffer.c_str(), "1") == 0);
}

TEST_CASE("integer_to_buffer max", "[NoDB]")
{
    util::integer_to_buffer buffer{std::numeric_limits<osmid_t>::max()};
    REQUIRE(std::strcmp(buffer.c_str(), "9223372036854775807") == 0);
}

TEST_CASE("integer_to_buffer min", "[NoDB]")
{
    util::integer_to_buffer buffer{std::numeric_limits<osmid_t>::min()};
    REQUIRE(std::strcmp(buffer.c_str(), "-9223372036854775808") == 0);
}

TEST_CASE("double_to_buffer 0", "[NoDB]")
{
    util::double_to_buffer buffer{0.0};
    REQUIRE(std::strcmp(buffer.c_str(), "0") == 0);
}

TEST_CASE("double_to_buffer 3.141", "[NoDB]")
{
    util::double_to_buffer buffer{3.141};
    REQUIRE(std::strcmp(buffer.c_str(), "3.141") == 0);
}

TEST_CASE("string_id_list_t with one element", "[NoDB]")
{
    util::string_id_list_t list;
    REQUIRE(list.empty());

    list.add(17);

    REQUIRE_FALSE(list.empty());
    REQUIRE(list.get() == "{17}");
}

TEST_CASE("string_id_list_t with several elements", "[NoDB]")
{
    util::string_id_list_t list;
    REQUIRE(list.empty());

    list.add(17);
    list.add(3);
    list.add(99);

    REQUIRE_FALSE(list.empty());
    REQUIRE(list.get() == "{17,3,99}");
}

TEST_CASE("human readable time durations", "[NoDB]")
{
    REQUIRE(util::human_readable_duration(0) == "0s");
    REQUIRE(util::human_readable_duration(17) == "17s");
    REQUIRE(util::human_readable_duration(59) == "59s");
    REQUIRE(util::human_readable_duration(60) == "60s (1m 0s)");
    REQUIRE(util::human_readable_duration(66) == "66s (1m 6s)");
    REQUIRE(util::human_readable_duration(247) == "247s (4m 7s)");
    REQUIRE(util::human_readable_duration(3599) == "3599s (59m 59s)");
    REQUIRE(util::human_readable_duration(3600) == "3600s (1h 0m 0s)");
    REQUIRE(util::human_readable_duration(3723) == "3723s (1h 2m 3s)");
    REQUIRE(util::human_readable_duration(152592) == "152592s (42h 23m 12s)");
}

