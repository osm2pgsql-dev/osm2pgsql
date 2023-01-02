/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <catch.hpp>

#include "flex-lua-index.hpp"
#include "flex-table.hpp"
#include "lua.hpp"
#include "pgsql-capabilities-int.hpp"

class test_framework
{
public:
    test_framework()
    : m_lua_state(luaL_newstate(), [](lua_State *state) { lua_close(state); })
    {
        auto &c = database_capabilities_for_testing();

        c.settings = {};

        c.extensions = {"postgis"};
        c.schemas = {"testschema"};
        c.tablespaces = {"somewhereelse"};
        c.index_methods = {"gist", "btree"};

        c.database_version = 110000;
    }

    lua_State *lua_state() const noexcept { return m_lua_state.get(); }

    bool run_lua(char const *code) const
    {
        return luaL_dostring(lua_state(), code) == 0;
    }

private:
    std::shared_ptr<lua_State> m_lua_state;
};

TEST_CASE("check index with single column", "[NoDB]")
{
    test_framework tf;

    flex_table_t table{"test_table"};
    table.add_column("geom", "geometry", "");

    REQUIRE(table.indexes().empty());

    REQUIRE(tf.run_lua("return { method = 'gist', column = 'geom' }"));
    flex_lua_setup_index(tf.lua_state(), &table);

    REQUIRE(table.indexes().size() == 1);
    flex_index_t const &idx = table.indexes().front();
    REQUIRE(idx.method() == "gist");
    REQUIRE(idx.columns() == R"(("geom"))");
    REQUIRE_FALSE(idx.is_unique());
    REQUIRE(idx.tablespace().empty());
    REQUIRE(idx.expression().empty());
    REQUIRE(idx.where_condition().empty());
    REQUIRE(idx.include_columns().empty());
}

TEST_CASE("check index with multiple columns", "[NoDB]")
{
    test_framework tf;

    flex_table_t table{"test_table"};
    table.add_column("a", "int", "");
    table.add_column("b", "int", "");

    REQUIRE(tf.run_lua("return { method = 'btree', column = {'a', 'b'} }"));
    flex_lua_setup_index(tf.lua_state(), &table);

    REQUIRE(table.indexes().size() == 1);
    auto const &idx = table.indexes()[0];
    REQUIRE(idx.method() == "btree");
    REQUIRE(idx.columns() == R"(("a","b"))");
    REQUIRE_FALSE(idx.is_unique());
    REQUIRE(idx.tablespace().empty());
    REQUIRE(idx.expression().empty());
    REQUIRE(idx.where_condition().empty());
    REQUIRE(idx.include_columns().empty());
}

TEST_CASE("check unique index", "[NoDB]")
{
    test_framework tf;

    flex_table_t table{"test_table"};
    table.add_column("col", "int", "");

    REQUIRE(tf.run_lua(
        "return { method = 'btree', column = 'col', unique = true }"));
    flex_lua_setup_index(tf.lua_state(), &table);

    REQUIRE(table.indexes().size() == 1);
    auto const &idx = table.indexes()[0];
    REQUIRE(idx.method() == "btree");
    REQUIRE(idx.columns() == R"(("col"))");
    REQUIRE(idx.is_unique());
    REQUIRE(idx.tablespace().empty());
    REQUIRE(idx.expression().empty());
    REQUIRE(idx.where_condition().empty());
    REQUIRE(idx.include_columns().empty());
}

TEST_CASE("check index with tablespace from table", "[NoDB]")
{
    test_framework tf;

    flex_table_t table{"test_table"};
    table.set_index_tablespace("foo");
    table.add_column("col", "int", "");

    REQUIRE(tf.run_lua("return { method = 'btree', column = 'col' }"));
    flex_lua_setup_index(tf.lua_state(), &table);

    REQUIRE(table.indexes().size() == 1);
    auto const &idx = table.indexes()[0];
    REQUIRE(idx.method() == "btree");
    REQUIRE(idx.columns() == R"(("col"))");
    REQUIRE_FALSE(idx.is_unique());
    REQUIRE(idx.tablespace() == "foo");
    REQUIRE(idx.expression().empty());
    REQUIRE(idx.where_condition().empty());
    REQUIRE(idx.include_columns().empty());
}

TEST_CASE("check index with tablespace", "[NoDB]")
{
    test_framework tf;

    flex_table_t table{"test_table"};
    table.add_column("col", "int", "");

    REQUIRE(tf.run_lua("return { method = 'btree', column = 'col', tablespace "
                       "= 'somewhereelse' }"));
    flex_lua_setup_index(tf.lua_state(), &table);

    REQUIRE(table.indexes().size() == 1);
    auto const &idx = table.indexes()[0];
    REQUIRE(idx.method() == "btree");
    REQUIRE(idx.columns() == R"(("col"))");
    REQUIRE_FALSE(idx.is_unique());
    REQUIRE(idx.tablespace() == "somewhereelse");
    REQUIRE(idx.expression().empty());
    REQUIRE(idx.where_condition().empty());
    REQUIRE(idx.include_columns().empty());
}

TEST_CASE("check index with expression and where clause", "[NoDB]")
{
    test_framework tf;

    flex_table_t table{"test_table"};
    table.add_column("col", "text", "");

    REQUIRE(tf.run_lua("return { method = 'btree', expression = 'lower(col)',"
                       " where = 'length(col) > 1' }"));
    flex_lua_setup_index(tf.lua_state(), &table);

    REQUIRE(table.indexes().size() == 1);
    auto const &idx = table.indexes()[0];
    REQUIRE(idx.method() == "btree");
    REQUIRE(idx.columns().empty());
    REQUIRE_FALSE(idx.is_unique());
    REQUIRE(idx.tablespace().empty());
    REQUIRE(idx.expression() == "lower(col)");
    REQUIRE(idx.where_condition() == "length(col) > 1");
    REQUIRE(idx.include_columns().empty());
}

TEST_CASE("check index with include", "[NoDB]")
{
    test_framework tf;

    flex_table_t table{"test_table"};
    table.add_column("col", "int", "");
    table.add_column("extra", "int", "");

    REQUIRE(tf.run_lua(
        "return { method = 'btree', column = 'col', include = 'extra' }"));
    flex_lua_setup_index(tf.lua_state(), &table);

    REQUIRE(table.indexes().size() == 1);
    auto const &idx = table.indexes()[0];
    REQUIRE(idx.method() == "btree");
    REQUIRE(idx.columns() == R"(("col"))");
    REQUIRE_FALSE(idx.is_unique());
    REQUIRE(idx.tablespace().empty());
    REQUIRE(idx.expression().empty());
    REQUIRE(idx.where_condition().empty());
    REQUIRE(idx.include_columns() == R"(("extra"))");
}

TEST_CASE("check index with include as array", "[NoDB]")
{
    test_framework tf;

    flex_table_t table{"test_table"};
    table.add_column("col", "int", "");
    table.add_column("extra", "int", "");

    REQUIRE(tf.run_lua(
        "return { method = 'btree', column = 'col', include = { 'extra' } }"));
    flex_lua_setup_index(tf.lua_state(), &table);

    REQUIRE(table.indexes().size() == 1);
    auto const &idx = table.indexes()[0];
    REQUIRE(idx.method() == "btree");
    REQUIRE(idx.columns() == R"(("col"))");
    REQUIRE_FALSE(idx.is_unique());
    REQUIRE(idx.tablespace().empty());
    REQUIRE(idx.expression().empty());
    REQUIRE(idx.where_condition().empty());
    REQUIRE(idx.include_columns() == R"(("extra"))");
}

TEST_CASE("check index with empty include array", "[NoDB]")
{
    test_framework tf;

    flex_table_t table{"test_table"};
    table.add_column("col", "int", "");
    table.add_column("extra", "int", "");

    REQUIRE(tf.run_lua(
        "return { method = 'btree', column = 'col', include = {} }"));
    flex_lua_setup_index(tf.lua_state(), &table);

    REQUIRE(table.indexes().size() == 1);
    auto const &idx = table.indexes()[0];
    REQUIRE(idx.method() == "btree");
    REQUIRE(idx.columns() == R"(("col"))");
    REQUIRE_FALSE(idx.is_unique());
    REQUIRE(idx.tablespace().empty());
    REQUIRE(idx.expression().empty());
    REQUIRE(idx.where_condition().empty());
    REQUIRE(idx.include_columns().empty());
}

TEST_CASE("check multiple indexes", "[NoDB]")
{
    test_framework tf;

    flex_table_t table{"test_table"};
    table.add_column("a", "int", "");
    table.add_column("b", "int", "");

    REQUIRE(tf.run_lua("return { method = 'btree', column = {'a'} }"));
    flex_lua_setup_index(tf.lua_state(), &table);
    REQUIRE(tf.run_lua("return { method = 'gist', column = 'b' }"));
    flex_lua_setup_index(tf.lua_state(), &table);

    REQUIRE(table.indexes().size() == 2);
    auto const &idx0 = table.indexes()[0];
    REQUIRE(idx0.method() == "btree");
    REQUIRE(idx0.columns() == R"(("a"))");

    auto const &idx1 = table.indexes()[1];
    REQUIRE(idx1.method() == "gist");
    REQUIRE(idx1.columns() == R"(("b"))");
}

TEST_CASE("check various broken index configs", "[NoDB]")
{
    test_framework tf;

    flex_table_t table{"test_table"};
    table.add_column("col", "text", "");

    SECTION("empty index description") { REQUIRE(tf.run_lua("return {}")); }

    SECTION("missing method")
    {
        REQUIRE(tf.run_lua("return { column = 'col' }"));
    }
    SECTION("non-existent method")
    {
        REQUIRE(tf.run_lua("return { method = 'abc', column = 'col' }"));
    }
    SECTION("wrong type for method")
    {
        REQUIRE(tf.run_lua("return { method = 123, column = 'col' }"));
    }
    SECTION("non-existent column")
    {
        REQUIRE(tf.run_lua("return { method = 'btree', column = 'x' }"));
    }
    SECTION("wrong type for column")
    {
        REQUIRE(tf.run_lua("return { method = 'btree', column = true }"));
    }
    SECTION("empty array for column")
    {
        REQUIRE(tf.run_lua("return { method = 'btree', column = {} }"));
    }
    SECTION("wrong type for expression")
    {
        REQUIRE(tf.run_lua("return { method = 'btree', expression = true }"));
    }
    SECTION("column and expression")
    {
        REQUIRE(tf.run_lua("return { method = 'btree', column = 'col', "
                           "expression = 'lower(col)' }"));
    }
    SECTION("non-existent tablespace")
    {
        REQUIRE(tf.run_lua(
            "return { method = 'btree', column = 'col', tablespace = 'not' }"));
    }
    SECTION("wrong type for tablespace")
    {
        REQUIRE(tf.run_lua(
            "return { method = 'btree', column = 'col', tablespace = 1.3 }"));
    }
    SECTION("wrong type for unique")
    {
        REQUIRE(tf.run_lua(
            "return { method = 'btree', column = 'col', unique = 1 }"));
    }
    SECTION("wrong type for where condition")
    {
        REQUIRE(tf.run_lua(
            "return { method = 'btree', column = 'col', where = {} }"));
    }
    SECTION("wrong type for include")
    {
        REQUIRE(tf.run_lua(
            "return { btree = 'btree', column = 'col', include = 1.2 }"));
    }
    SECTION("unknown column for include")
    {
        REQUIRE(tf.run_lua(
            "return { btree = 'btree', column = 'col', include = 'foo' }"));
    }

    REQUIRE_THROWS(flex_lua_setup_index(tf.lua_state(), &table));
}
