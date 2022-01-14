/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <catch.hpp>

#include "ordered-index.hpp"

TEST_CASE("ordered index basics", "[NoDB]")
{
    constexpr std::size_t const block_size = 16;
    ordered_index_t index{block_size};

    REQUIRE(index.size() == 0);
    REQUIRE(index.capacity() == 0);
    REQUIRE(index.used_memory() == 0);

    index.add(17, 32);
    REQUIRE(index.size() == 1);
    REQUIRE(index.capacity() == block_size);
    REQUIRE(index.used_memory() > 0);

    index.add(19, 33);
    REQUIRE(index.size() == 2);
    REQUIRE(index.capacity() == block_size);
    REQUIRE(index.used_memory() > 0);

    index.clear();
    REQUIRE(index.size() == 0);
    REQUIRE(index.capacity() == 0);
    REQUIRE(index.used_memory() == 0);
}

TEST_CASE("ordered index set/get", "[NoDB]")
{
    constexpr std::size_t const block_size = 16;
    ordered_index_t index{block_size};

    index.add(19, 0);
    index.add(22, 10);
    index.add(23, 22);
    index.add(26, 24);
    REQUIRE(index.size() == 4);

    REQUIRE(index.get(22) == 10);
    REQUIRE(index.get(23) == 22);
    REQUIRE(index.get(26) == 24);
    REQUIRE(index.get(19) == 0);

    REQUIRE(index.get(0) == index.not_found_value());
    REQUIRE(index.get(20) == index.not_found_value());
    REQUIRE(index.get(27) == index.not_found_value());

    REQUIRE(index.get_block(0) == index.not_found_value());
    REQUIRE(index.get_block(20) == 0);
    REQUIRE(index.get_block(27) == 24);
    REQUIRE(index.get_block(99999) == 24);
}

TEST_CASE("ordered index set/get with multiple second-level blocks", "[NoDB]")
{
    constexpr std::size_t const block_size = 4;
    ordered_index_t index{block_size};

    index.add(19, 0);
    index.add(22, 10);
    index.add(23, 22);
    index.add(26, 24);
    REQUIRE(index.size() == 4);
    REQUIRE(index.capacity() == block_size);

    REQUIRE(index.get(31) == index.not_found_value());

    index.add(31, 25);
    index.add(42, 30);
    index.add(65, 32);
    REQUIRE(index.size() == 7);
    REQUIRE(index.capacity() == block_size * (1 + 2));

    REQUIRE(index.get(22) == 10);
    REQUIRE(index.get(23) == 22);
    REQUIRE(index.get(26) == 24);
    REQUIRE(index.get(19) == 0);
    REQUIRE(index.get(42) == 30);
    REQUIRE(index.get(31) == 25);
    REQUIRE(index.get(65) == 32);

    REQUIRE(index.get(0) == index.not_found_value());
    REQUIRE(index.get(27) == index.not_found_value());
    REQUIRE(index.get(30) == index.not_found_value());
    REQUIRE(index.get(66) == index.not_found_value());
    REQUIRE(index.get(99) == index.not_found_value());

    REQUIRE(index.get_block(0) == index.not_found_value());
    REQUIRE(index.get_block(18) == index.not_found_value());
    REQUIRE(index.get_block(22) == 10);
    REQUIRE(index.get_block(24) == 22);
    REQUIRE(index.get_block(50) == 30);
    REQUIRE(index.get_block(66) == 32);
}

TEST_CASE("ordered index with huge gaps in ids", "[NoDB]")
{
    constexpr std::size_t const block_size = 4;
    ordered_index_t index{block_size};

    index.add(1, 0);
    REQUIRE(index.size() == 1);
    REQUIRE(index.capacity() == block_size);

    index.add((1ULL << 32U) + 3U, 1);
    REQUIRE(index.size() == 2);
    REQUIRE(index.capacity() == block_size * (1 + 2));

    index.add((1ULL << 32U) + 4U, 2);
    REQUIRE(index.size() == 3);
    REQUIRE(index.capacity() == block_size * (1 + 2));

    index.add((2ULL << 32U) + 9U, 3);
    REQUIRE(index.size() == 4);
    REQUIRE(index.capacity() == block_size * (1 + 2 + 4));

    REQUIRE(index.used_memory() > (index.capacity() * 8));

    REQUIRE(index.get(1) == 0);
    REQUIRE(index.get((1ULL << 32U) + 3U) == 1);
    REQUIRE(index.get((1ULL << 32U) + 4U) == 2);
    REQUIRE(index.get((2ULL << 32U) + 9U) == 3);
    REQUIRE(index.get(2) == index.not_found_value());

    REQUIRE(index.get_block(1) == 0);
    REQUIRE(index.get_block(2) == 0);
    REQUIRE(index.get_block((1ULL << 32U) + 2U) == 0);
    REQUIRE(index.get_block((1ULL << 32U) + 3U) == 1);
    REQUIRE(index.get_block((1ULL << 32U) + 4U) == 2);
    REQUIRE(index.get_block((1ULL << 32U) + 5U) == 2);
    REQUIRE(index.get_block((2ULL << 32U) + 8U) == 2);
    REQUIRE(index.get_block((2ULL << 32U) + 9U) == 3);
    REQUIRE(index.get_block((3ULL << 32U) + 2U) == 3);
}

