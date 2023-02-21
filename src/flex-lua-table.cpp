/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "flex-lua-table.hpp"

#include "expire-output.hpp"
#include "flex-lua-index.hpp"
#include "flex-table.hpp"
#include "lua-utils.hpp"
#include "pgsql-capabilities.hpp"

#include <lua.hpp>

static void check_tablespace(std::string const &tablespace)
{
    if (!has_tablespace(tablespace)) {
        throw fmt_error(
            "Tablespace '{0}' not available."
            " Use 'CREATE TABLESPACE \"{0}\" ...;' to create it.",
            tablespace);
    }
}

static flex_table_t &create_flex_table(lua_State *lua_state,
                                       std::vector<flex_table_t> *tables)
{
    std::string const table_name =
        luaX_get_table_string(lua_state, "name", -1, "The table");

    check_identifier(table_name, "table names");

    if (util::find_by_name(*tables, table_name)) {
        throw fmt_error("Table with name '{}' already exists.", table_name);
    }

    auto &new_table = tables->emplace_back(table_name);

    lua_pop(lua_state, 1); // "name"

    // optional "schema" field
    lua_getfield(lua_state, -1, "schema");
    if (lua_isstring(lua_state, -1)) {
        std::string const schema = lua_tostring(lua_state, -1);
        check_identifier(schema, "schema field");
        if (!has_schema(schema)) {
            throw fmt_error("Schema '{0}' not available."
                            " Use 'CREATE SCHEMA \"{0}\";' to create it.",
                            schema);
        }
        new_table.set_schema(schema);
    }
    lua_pop(lua_state, 1);

    // optional "cluster" field
    lua_getfield(lua_state, -1, "cluster");
    int const cluster_type = lua_type(lua_state, -1);
    if (cluster_type == LUA_TSTRING) {
        std::string const cluster = lua_tostring(lua_state, -1);
        if (cluster == "auto") {
            new_table.set_cluster_by_geom(true);
        } else if (cluster == "no") {
            new_table.set_cluster_by_geom(false);
        } else {
            throw fmt_error("Unknown value '{}' for 'cluster' table option"
                            " (use 'auto' or 'no').",
                            cluster);
        }
    } else if (cluster_type == LUA_TNIL) {
        // ignore
    } else {
        throw std::runtime_error{
            "Unknown value for 'cluster' table option: Must be string."};
    }
    lua_pop(lua_state, 1);

    // optional "data_tablespace" field
    lua_getfield(lua_state, -1, "data_tablespace");
    if (lua_isstring(lua_state, -1)) {
        std::string const tablespace = lua_tostring(lua_state, -1);
        check_identifier(tablespace, "data_tablespace field");
        check_tablespace(tablespace);
        new_table.set_data_tablespace(tablespace);
    }
    lua_pop(lua_state, 1);

    // optional "index_tablespace" field
    lua_getfield(lua_state, -1, "index_tablespace");
    if (lua_isstring(lua_state, -1)) {
        std::string const tablespace = lua_tostring(lua_state, -1);
        check_identifier(tablespace, "index_tablespace field");
        check_tablespace(tablespace);
        new_table.set_index_tablespace(tablespace);
    }
    lua_pop(lua_state, 1);

    return new_table;
}

static void setup_flex_table_id_columns(lua_State *lua_state,
                                        flex_table_t *table)
{
    assert(lua_state);
    assert(table);

    lua_getfield(lua_state, -1, "ids");
    if (lua_type(lua_state, -1) != LUA_TTABLE) {
        log_warn("Table '{}' doesn't have an id column. Two-stage"
                 " processing, updates and expire will not work!",
                 table->name());
        lua_pop(lua_state, 1); // ids
        return;
    }

    std::string const type{
        luaX_get_table_string(lua_state, "type", -1, "The ids field")};
    lua_pop(lua_state, 1); // "type"

    if (type == "node") {
        table->set_id_type(osmium::item_type::node);
    } else if (type == "way") {
        table->set_id_type(osmium::item_type::way);
    } else if (type == "relation") {
        table->set_id_type(osmium::item_type::relation);
    } else if (type == "area") {
        table->set_id_type(osmium::item_type::area);
    } else if (type == "any") {
        table->set_id_type(osmium::item_type::undefined);
        lua_getfield(lua_state, -1, "type_column");
        if (lua_isstring(lua_state, -1)) {
            std::string const column_name =
                lua_tolstring(lua_state, -1, nullptr);
            check_identifier(column_name, "column names");
            auto &column = table->add_column(column_name, "id_type", "");
            column.set_not_null();
        } else if (!lua_isnil(lua_state, -1)) {
            throw std::runtime_error{"type_column must be a string or nil."};
        }
        lua_pop(lua_state, 1); // "type_column"
    } else {
        throw fmt_error("Unknown ids type: {}.", type);
    }

    std::string const name =
        luaX_get_table_string(lua_state, "id_column", -1, "The ids field");
    lua_pop(lua_state, 1); // "id_column"
    check_identifier(name, "column names");

    std::string const create_index = luaX_get_table_string(
        lua_state, "create_index", -1, "The ids field", "auto");
    lua_pop(lua_state, 1); // "create_index"
    if (create_index == "always") {
        table->set_always_build_id_index();
    } else if (create_index != "auto") {
        throw fmt_error("Unknown value '{}' for 'create_index' field of ids",
                        create_index);
    }

    auto &column = table->add_column(name, "id_num", "");
    column.set_not_null();
    lua_pop(lua_state, 1); // "ids"
}

static std::size_t
find_expire_output(std::vector<expire_output_t> const &expire_outputs,
                   std::string_view name)
{
    std::size_t n = 0;
    for (auto const &eo : expire_outputs) {
        if (eo.name() == name) {
            return n;
        }
        ++n;
    }

    throw fmt_error("Unknown ExpireOutput '{}'.", name);
}

static void
parse_and_set_expire_options(lua_State *lua_state, flex_table_column_t *column,
                             std::vector<expire_output_t> *expire_outputs,
                             bool append_mode)
{
    auto const type = lua_type(lua_state, -1);

    if (type == LUA_TNIL) {
        return;
    }

    if (type == LUA_TSTRING) {
        auto eo =
            find_expire_output(*expire_outputs, lua_tostring(lua_state, -1));
        expire_config_t config{eo};
        // Actually add the expire only if we are in append mode.
        if (append_mode) {
            column->add_expire(config);
        }
        return;
    }

    if (type != LUA_TTABLE) {
        throw std::runtime_error{"Expire field must be a Lua array table"};
    }

    if (luaX_is_empty_table(lua_state)) {
        return;
    }

    if (!luaX_is_array(lua_state)) {
        throw std::runtime_error{"Expire field must be a Lua array table"};
    }

    if (!column->is_geometry_column() || column->srid() != 3857) {
        throw std::runtime_error{"Expire only allowed for geometry"
                                 " columns in Web Mercator projection."};
    }

    luaX_for_each(lua_state, [&]() {
        if (!lua_istable(lua_state, -1) || luaX_is_array(lua_state)) {
            throw std::runtime_error{"Expire config must be a Lua table"};
        }

        auto const *name =
            luaX_get_table_string(lua_state, "output", -1, "Entry 'output'");
        auto eo = find_expire_output(*expire_outputs, name);
        lua_pop(lua_state, 1); // "output"

        expire_config_t config{eo};

        std::string mode;
        lua_getfield(lua_state, -1, "mode");
        if (lua_isstring(lua_state, -1)) {
            mode = lua_tostring(lua_state, -1);
        } else if (!lua_isnil(lua_state, -1)) {
            throw std::runtime_error{
                "Optional expire field 'mode' must contain a string."};
        }
        lua_pop(lua_state, 1); // "mode"

        if (mode.empty() || mode == "full-area") {
            config.mode = expire_mode::full_area;
        } else if (mode == "boundary-only") {
            config.mode = expire_mode::boundary_only;
        } else if (mode == "hybrid") {
            config.mode = expire_mode::hybrid;
        } else {
            throw fmt_error("Unknown expire mode '{}'.", mode);
        }

        lua_getfield(lua_state, -1, "full_area_limit");
        if (lua_isnumber(lua_state, -1)) {
            if (config.mode != expire_mode::hybrid) {
                log_warn("Ignoring 'full_area_limit' setting in expire config,"
                         " because 'mode' is not set to 'hybrid'.");
            }
            config.full_area_limit = lua_tonumber(lua_state, -1);
        } else if (!lua_isnil(lua_state, -1)) {
            throw std::runtime_error{"Optional expire field 'full_area_limit' "
                                     "must contain a number."};
        }
        lua_pop(lua_state, 1); // "full_area_limit"

        lua_getfield(lua_state, -1, "buffer");
        if (lua_isnumber(lua_state, -1)) {
            config.buffer = lua_tonumber(lua_state, -1);
        } else if (!lua_isnil(lua_state, -1)) {
            throw std::runtime_error{
                "Optional expire field 'buffer' must contain a number."};
        }
        lua_pop(lua_state, 1); // "buffer"

        // Actually add the expire only if we are in append mode.
        if (append_mode) {
            column->add_expire(config);
        }
    });
}

static void
setup_flex_table_columns(lua_State *lua_state, flex_table_t *table,
                         std::vector<expire_output_t> *expire_outputs,
                         bool append_mode)
{
    assert(lua_state);
    assert(table);

    lua_getfield(lua_state, -1, "columns");
    if (lua_type(lua_state, -1) != LUA_TTABLE) {
        throw fmt_error("No 'columns' field (or not an array) in table '{}'.",
                        table->name());
    }

    if (!luaX_is_array(lua_state)) {
        throw std::runtime_error{"The 'columns' field must contain an array."};
    }
    std::size_t num_columns = 0;
    luaX_for_each(lua_state, [&]() {
        if (!lua_istable(lua_state, -1)) {
            throw std::runtime_error{
                "The entries in the 'columns' array must be tables."};
        }

        char const *const type = luaX_get_table_string(lua_state, "type", -1,
                                                       "Column entry", "text");
        char const *const name =
            luaX_get_table_string(lua_state, "column", -2, "Column entry");
        check_identifier(name, "column names");
        char const *const sql_type = luaX_get_table_string(
            lua_state, "sql_type", -3, "Column entry", "");

        auto &column = table->add_column(name, type, sql_type);
        lua_pop(lua_state, 3); // "type", "column", "sql_type"

        column.set_not_null(luaX_get_table_bool(lua_state, "not_null", -1,
                                                "Entry 'not_null'", false));
        lua_pop(lua_state, 1); // "not_null"

        column.set_create_only(luaX_get_table_bool(
            lua_state, "create_only", -1, "Entry 'create_only'", false));
        lua_pop(lua_state, 1); // "create_only"

        lua_getfield(lua_state, -1, "projection");
        if (!lua_isnil(lua_state, -1)) {
            if (column.is_geometry_column() ||
                column.type() == table_column_type::area) {
                column.set_projection(lua_tostring(lua_state, -1));
            } else {
                throw std::runtime_error{"Projection can only be set on "
                                         "geometry and area columns."};
            }
        }
        lua_pop(lua_state, 1); // "projection"

        lua_getfield(lua_state, -1, "expire");
        parse_and_set_expire_options(lua_state, &column, expire_outputs,
                                     append_mode);
        lua_pop(lua_state, 1); // "expire"

        ++num_columns;
    });

    if (num_columns == 0 && !table->has_id_column()) {
        throw fmt_error("No columns defined for table '{}'.", table->name());
    }

    lua_pop(lua_state, 1); // "columns"
}

static void setup_flex_table_indexes(lua_State *lua_state, flex_table_t *table,
                                     bool updatable)
{
    assert(lua_state);
    assert(table);

    lua_getfield(lua_state, -1, "indexes");
    if (lua_type(lua_state, -1) == LUA_TNIL) {
        if (table->has_geom_column()) {
            auto &index = table->add_index("gist");
            index.set_columns(table->geom_column().name());

            if (!updatable) {
                // If database can not be updated, use fillfactor 100.
                index.set_fillfactor(100);
            }
            index.set_tablespace(table->index_tablespace());
        }
        lua_pop(lua_state, 1); // "indexes"
        return;
    }

    if (lua_type(lua_state, -1) != LUA_TTABLE) {
        throw fmt_error("The 'indexes' field in definition of"
                        " table '{}' is not an array.",
                        table->name());
    }

    if (!luaX_is_array(lua_state)) {
        throw std::runtime_error{"The 'indexes' field must contain an array."};
    }

    luaX_for_each(lua_state, [&]() {
        if (!lua_istable(lua_state, -1)) {
            throw std::runtime_error{
                "The entries in the 'indexes' array must be Lua tables."};
        }

        flex_lua_setup_index(lua_state, table);
    });

    lua_pop(lua_state, 1); // "indexes"
}

int setup_flex_table(lua_State *lua_state, std::vector<flex_table_t> *tables,
                     std::vector<expire_output_t> *expire_outputs,
                     bool updatable, bool append_mode)
{
    if (lua_type(lua_state, 1) != LUA_TTABLE) {
        throw std::runtime_error{
            "Argument #1 to 'define_table' must be a table."};
    }

    auto &new_table = create_flex_table(lua_state, tables);
    setup_flex_table_id_columns(lua_state, &new_table);
    setup_flex_table_columns(lua_state, &new_table, expire_outputs,
                             append_mode);
    setup_flex_table_indexes(lua_state, &new_table, updatable);

    void *ptr = lua_newuserdata(lua_state, sizeof(std::size_t));
    auto *num = new (ptr) std::size_t{};
    *num = tables->size() - 1;
    luaL_getmetatable(lua_state, osm2pgsql_table_name);
    lua_setmetatable(lua_state, -2);

    return 1;
}
