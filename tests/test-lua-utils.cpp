/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <catch.hpp>

#include "lua-utils.hpp"

extern "C"
{
#include <lauxlib.h>
}

// Run the Lua code in "code" and then execute the function "func".
template <typename FUNC>
void test_lua(lua_State *lua_state, char const *code, FUNC&& func) {
    REQUIRE(lua_gettop(lua_state) == 0);
    REQUIRE(luaL_dostring(lua_state, code) == 0);
    REQUIRE(lua_gettop(lua_state) == 1);
    std::forward<FUNC>(func)();
    REQUIRE(lua_gettop(lua_state) == 1);
    lua_pop(lua_state, 1); // result from executing the Lua code
    REQUIRE(lua_gettop(lua_state) == 0);
}

TEST_CASE("check luaX_is_empty_table", "[NoDB]")
{
    std::shared_ptr<lua_State> lua_state{
        luaL_newstate(), [](lua_State *state) { lua_close(state); }};

    test_lua(lua_state.get(), "return {}", [&](){
        REQUIRE(luaX_is_empty_table(lua_state.get()));
    });

    test_lua(lua_state.get(), "return { 1 }", [&](){
        REQUIRE_FALSE(luaX_is_empty_table(lua_state.get()));
    });

    test_lua(lua_state.get(), "return { a = 'b' }", [&](){
        REQUIRE_FALSE(luaX_is_empty_table(lua_state.get()));
    });
}

TEST_CASE("check luaX_is_array with arrays", "[NoDB]")
{
    std::shared_ptr<lua_State> lua_state{
        luaL_newstate(), [](lua_State *state) { lua_close(state); }};

    test_lua(lua_state.get(), "return { 1, 2, 3 }", [&](){
        REQUIRE(luaX_is_array(lua_state.get()));
    });

    test_lua(lua_state.get(), "return { }", [&](){
        REQUIRE(luaX_is_array(lua_state.get()));
    });

    test_lua(lua_state.get(), "return { 1 }", [&](){
        REQUIRE(luaX_is_array(lua_state.get()));
    });

    test_lua(lua_state.get(), "return { [1] = 1, [2] = 2 }", [&](){
        REQUIRE(luaX_is_array(lua_state.get()));
    });
}

TEST_CASE("check luaX_is_array with non-arrays", "[NoDB]")
{
    std::shared_ptr<lua_State> lua_state{
        luaL_newstate(), [](lua_State *state) { lua_close(state); }};

    test_lua(lua_state.get(), "return { 1, nil, 3 }", [&](){
        REQUIRE_FALSE(luaX_is_array(lua_state.get()));
    });

    test_lua(lua_state.get(), "return { a = 'foo' }", [&](){
        REQUIRE_FALSE(luaX_is_array(lua_state.get()));
    });

    test_lua(lua_state.get(), "return { [1] = 'foo', ['bar'] = 2 }", [&](){
        REQUIRE_FALSE(luaX_is_array(lua_state.get()));
    });
}

TEST_CASE("luaX_for_each should call function n times", "[NoDB]")
{
    std::shared_ptr<lua_State> lua_state{
        luaL_newstate(), [](lua_State *state) { lua_close(state); }};

    test_lua(lua_state.get(), "return { 3, 4, 5 }", [&](){
        int sum = 0;
        luaX_for_each(lua_state.get(), [&]() {
            sum += lua_tonumber(lua_state.get(), -1);
        });
        REQUIRE(sum == 12);
    });
}

TEST_CASE("luaX_for_each should not call the function for empty arrays",
          "[NoDB]")
{
    std::shared_ptr<lua_State> lua_state{
        luaL_newstate(), [](lua_State *state) { lua_close(state); }};

    bool called = false;
    test_lua(lua_state.get(), "return {}", [&]() {
        luaX_for_each(lua_state.get(), [&]() { called = true; });
    });
    REQUIRE_FALSE(called);
}
