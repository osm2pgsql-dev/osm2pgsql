/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "flex-lua-index.hpp"
#include "lua-utils.hpp"
#include "output-flex.hpp"
#include "pgsql-capabilities.hpp"
#include "util.hpp"

#include <string>
#include <vector>

static void check_and_add_column(flex_table_t const &table,
                                 std::vector<std::string> *columns,
                                 char const *column_name)
{
    auto const *column = util::find_by_name(table, column_name);
    if (!column) {
        throw std::runtime_error{"Unknown column '{}' in table '{}'."_format(
            column_name, table.name())};
    }
    columns->push_back(column_name);
}

static void check_and_add_columns(flex_table_t const &table,
                                  std::vector<std::string> *columns,
                                  lua_State *lua_state)
{
    lua_pushnil(lua_state);
    while (lua_next(lua_state, -2) != 0) {
        if (!lua_isnumber(lua_state, -2)) {
            throw std::runtime_error{
                "The 'column' field must contain a string or an array."};
        }
        if (!lua_isstring(lua_state, -1)) {
            throw std::runtime_error{
                "The entries in the 'column' array must be strings."};
        }
        check_and_add_column(table, columns, lua_tostring(lua_state, -1));
        lua_pop(lua_state, 1); // table
    }
}

void flex_lua_setup_index(lua_State *lua_state, flex_table_t *table)
{
    char const *const method =
        luaX_get_table_string(lua_state, "method", -1, "Index definition");
    if (!has_index_method(method)) {
        throw std::runtime_error{"Unknown index method '{}'."_format(method)};
    }

    auto &index = table->add_index(method);

    std::vector<std::string> columns;
    lua_getfield(lua_state, -2, "column");
    if (lua_isstring(lua_state, -1)) {
        check_and_add_column(*table, &columns, lua_tostring(lua_state, -1));
        index.set_columns(columns);
    } else if (lua_istable(lua_state, -1)) {
        check_and_add_columns(*table, &columns, lua_state);
        if (columns.empty()) {
            throw std::runtime_error{
                "The 'column' field in an index definition can not be an "
                "empty array."};
        }
        index.set_columns(columns);
    } else if (!lua_isnil(lua_state, -1)) {
        throw std::runtime_error{
            "The 'column' field in an index definition must contain a "
            "string or an array."};
    }

    std::string const expression = luaX_get_table_string(
        lua_state, "expression", -3, "Index definition", "");

    index.set_expression(expression);

    if (expression.empty() == columns.empty()) {
        throw std::runtime_error{"You must set either the 'column' or the "
                                 "'expression' field in index definition."};
    }

    std::vector<std::string> include_columns;
    lua_getfield(lua_state, -4, "include");
    if (get_database_version() >= 110000) {
        if (lua_isstring(lua_state, -1)) {
            check_and_add_column(*table, &include_columns,
                                 lua_tostring(lua_state, -1));
        } else if (lua_istable(lua_state, -1)) {
            check_and_add_columns(*table, &include_columns, lua_state);
        } else if (!lua_isnil(lua_state, -1)) {
            throw std::runtime_error{
                "The 'include' field in an index definition must contain a "
                "string or an array."};
        }
        index.set_include_columns(include_columns);
    } else if (!lua_isnil(lua_state, -1)) {
        throw std::runtime_error{
            "Database version ({}) doesn't support"
            " include columns in indexes."_format(get_database_version())};
    }

    std::string const tablespace = luaX_get_table_string(
        lua_state, "tablespace", -5, "Index definition", "");
    check_identifier(tablespace, "tablespace");
    if (!has_tablespace(tablespace)) {
        throw std::runtime_error{"Unknown tablespace '{}'."_format(tablespace)};
    }
    index.set_tablespace(tablespace.empty() ? table->index_tablespace()
                                            : tablespace);

    index.set_is_unique(luaX_get_table_bool(lua_state, "unique", -6,
                                            "Index definition", false));

    index.set_where_condition(
        luaX_get_table_string(lua_state, "where", -7, "Index definition", ""));

    // stack has: "where", "unique", "tablespace", "includes", "expression",
    // "column", "method"
    lua_pop(lua_state, 7);
}
