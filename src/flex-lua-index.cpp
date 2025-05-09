/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2025 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "flex-lua-index.hpp"

#include "flex-index.hpp"
#include "flex-table.hpp"
#include "format.hpp"
#include "lua-utils.hpp"
#include "pgsql-capabilities.hpp"
#include "util.hpp"

#include <stdexcept>
#include <string>
#include <vector>

namespace {

void check_and_add_column(flex_table_t const &table,
                          std::vector<std::string> *columns,
                          char const *column_name)
{
    auto const *column = util::find_by_name(table.columns(), column_name);
    if (!column) {
        throw fmt_error("Unknown column '{}' in table '{}'.", column_name,
                        table.name());
    }
    columns->emplace_back(column_name);
}

void check_and_add_columns(flex_table_t const &table,
                           std::vector<std::string> *columns,
                           lua_State *lua_state)
{
    if (!luaX_is_array(lua_state)) {
        throw std::runtime_error{
            "The 'column' field must contain a string or an array."};
    }

    luaX_for_each(lua_state, [&]() {
        if (!lua_isstring(lua_state, -1)) {
            throw std::runtime_error{
                "The entries in the 'column' array must be strings."};
        }
        check_and_add_column(table, columns, lua_tostring(lua_state, -1));
    });
}

} // anonymous namespace

void flex_lua_setup_index(lua_State *lua_state, flex_table_t *table)
{
    // get method
    char const *const method =
        luaX_get_table_string(lua_state, "method", -1, "Index definition");
    if (!has_index_method(method)) {
        throw fmt_error("Unknown index method '{}'.", method);
    }
    lua_pop(lua_state, 1);
    auto &index = table->add_index(method);

    // get columns
    std::vector<std::string> columns;
    lua_getfield(lua_state, -1, "column");
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
    lua_pop(lua_state, 1);

    // get name
    std::string const name =
        luaX_get_table_string(lua_state, "name", -1, "Index definition", "");
    lua_pop(lua_state, 1);
    index.set_name(name);

    // get expression
    std::string const expression = luaX_get_table_string(
        lua_state, "expression", -1, "Index definition", "");
    lua_pop(lua_state, 1);
    if (expression.empty() == columns.empty()) {
        throw std::runtime_error{"You must set either the 'column' or the "
                                 "'expression' field in index definition."};
    }
    index.set_expression(expression);

    // get include columns
    std::vector<std::string> include_columns;
    lua_getfield(lua_state, -1, "include");
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
    lua_pop(lua_state, 1);

    // get tablespace
    std::string const tablespace = luaX_get_table_string(
        lua_state, "tablespace", -1, "Index definition", "");
    lua_pop(lua_state, 1);
    check_identifier(tablespace, "tablespace");
    if (!has_tablespace(tablespace)) {
        throw fmt_error("Unknown tablespace '{}'.", tablespace);
    }
    index.set_tablespace(tablespace.empty() ? table->index_tablespace()
                                            : tablespace);

    // get unique
    index.set_is_unique(luaX_get_table_bool(lua_state, "unique", -1,
                                            "Index definition", false));
    lua_pop(lua_state, 1);

    // get where condition
    index.set_where_condition(
        luaX_get_table_string(lua_state, "where", -1, "Index definition", ""));
    lua_pop(lua_state, 1);
}
