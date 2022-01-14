/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <catch.hpp>

#include "input.hpp"

TEST_CASE("it's good if input data is ordered", "[NoDB]")
{
    type_id const tiv1{osmium::item_type::node, 1};
    type_id const tiv2{osmium::item_type::node, 2};
    type_id const tiv3{osmium::item_type::way, 1};
    type_id const tiv4{osmium::item_type::way, 2};
    type_id const tiv5{osmium::item_type::relation, 1};
    type_id const tiv6{osmium::item_type::relation, 2};

    REQUIRE_NOTHROW(check_input(tiv1, tiv2));
    REQUIRE_NOTHROW(check_input(tiv2, tiv3));
    REQUIRE_NOTHROW(check_input(tiv3, tiv4));
    REQUIRE_NOTHROW(check_input(tiv4, tiv5));
    REQUIRE_NOTHROW(check_input(tiv5, tiv6));
}

TEST_CASE("negative OSM object ids are not allowed", "[NoDB]")
{
    type_id const tivn{osmium::item_type::node, -17};
    type_id const tivw{osmium::item_type::way, -1};
    type_id const tivr{osmium::item_type::relation, -999};

    REQUIRE_THROWS_WITH(
        check_input(tivn, tivn),
        "Negative OSM object ids are not allowed: node id -17.");
    REQUIRE_THROWS_WITH(check_input(tivw, tivw),
                        "Negative OSM object ids are not allowed: way id -1.");
    REQUIRE_THROWS_WITH(
        check_input(tivr, tivr),
        "Negative OSM object ids are not allowed: relation id -999.");
}

TEST_CASE("objects of the same type must be ordered", "[NoDB]")
{
    type_id const tiv1{osmium::item_type::node, 42};
    type_id const tiv2{osmium::item_type::node, 3};

    REQUIRE_THROWS_WITH(check_input(tiv1, tiv2),
                        "Input data is not ordered: node id 3 after 42.");
}

TEST_CASE("a node after a way or relation is not allowed", "[NoDB]")
{
    type_id const tiv1w{osmium::item_type::way, 42};
    type_id const tiv1r{osmium::item_type::relation, 42};
    type_id const tiv2{osmium::item_type::node, 100};

    REQUIRE_THROWS_WITH(check_input(tiv1w, tiv2),
                        "Input data is not ordered: node after way.");
    REQUIRE_THROWS_WITH(check_input(tiv1r, tiv2),
                        "Input data is not ordered: node after relation.");
}

TEST_CASE("a way after a relation is not allowed", "[NoDB]")
{
    type_id const tiv1{osmium::item_type::relation, 42};
    type_id const tiv2{osmium::item_type::way, 100};

    REQUIRE_THROWS_WITH(check_input(tiv1, tiv2),
                        "Input data is not ordered: way after relation.");
}

TEST_CASE("no object may appear twice", "[NoDB]")
{
    type_id const tiv1{osmium::item_type::way, 42};
    type_id const tiv2{osmium::item_type::way, 42};

    REQUIRE_THROWS_WITH(
        check_input(tiv1, tiv2),
        "Input data is not ordered: way id 42 appears more than once.");
}

