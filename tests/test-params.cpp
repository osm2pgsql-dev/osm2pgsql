/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2024 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <catch.hpp>

#include "params.hpp"

TEST_CASE("Set param value", "[NoDB]")
{
    param_value_t const p_null{null_param_t()};
    param_value_t const p_str{"foo"};
    param_value_t const p_int{26};
    param_value_t const p_double{3.141};
    param_value_t const p_true{true};
    param_value_t const p_false{false};

    REQUIRE(to_string(p_null).empty());
    REQUIRE(to_string(p_str) == "foo");
    REQUIRE(to_string(p_int) == "26");
    REQUIRE(to_string(p_double) == "3.141");
    REQUIRE(to_string(p_true) == "true");
    REQUIRE(to_string(p_false) == "false");
}

TEST_CASE("Params with different value types", "[NoDB]")
{
    params_t params;
    REQUIRE_FALSE(params.has("foo"));

    params.set("foo", 99);
    REQUIRE(params.has("foo"));
    REQUIRE(params.get("foo") == param_value_t(99));
    REQUIRE(params.get_int64("foo") == 99);

    params.set("foo", "astring");
    REQUIRE(params.has("foo"));
    REQUIRE(params.get("foo") == param_value_t("astring"));
    REQUIRE(params.get_string("foo") == "astring");
    REQUIRE_THROWS(params.get_int64("foo"));
}

TEST_CASE("Set params with explicit type", "[NoDB]")
{
    params_t params;

    params.set_null("isnull");
    params.set_string("isstring", "hi");
    params.set_int64("isint", 567);
    params.set_double("isdouble", 567);
    params.set_bool("istrue", true);
    params.set_bool("isfalse", false);

    REQUIRE(params.get("isnull") == param_value_t());
    REQUIRE(params.get_string("isstring") == "hi");
    REQUIRE(params.get_int64("isint") == 567);
    REQUIRE(params.get_double("isdouble") == Approx(567.0));
    REQUIRE(params.get_bool("istrue"));
    REQUIRE_FALSE(params.get_bool("isfalse"));
}