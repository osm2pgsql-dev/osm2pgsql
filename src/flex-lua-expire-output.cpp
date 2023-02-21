/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "flex-lua-expire-output.hpp"

#include "expire-output.hpp"
#include "format.hpp"
#include "lua-utils.hpp"
#include "pgsql.hpp"
#include "util.hpp"

#include <lua.hpp>

static expire_output_t &
create_expire_output(lua_State *lua_state,
                     std::vector<expire_output_t> *expire_outputs)
{
    std::string const expire_output_name =
        luaX_get_table_string(lua_state, "name", -1, "The expire output");

    check_identifier(expire_output_name, "expire output names");

    if (util::find_by_name(*expire_outputs, expire_output_name)) {
        throw fmt_error("Expire output with name '{}' already exists.",
                        expire_output_name);
    }

    auto &new_expire_output = expire_outputs->emplace_back(expire_output_name);

    lua_pop(lua_state, 1); // "name"

    // optional "filename" field
    auto const *filename = luaX_get_table_string(lua_state, "filename", -1,
                                                 "The expire output", "");
    new_expire_output.set_filename(filename);
    lua_pop(lua_state, 1); // "filename"

    // optional "schema" and "table" fields
    auto const *schema =
        luaX_get_table_string(lua_state, "schema", -1, "The expire output", "");
    check_identifier(schema, "schema field");
    auto const *table =
        luaX_get_table_string(lua_state, "table", -2, "The expire output", "");
    check_identifier(table, "table field");
    new_expire_output.set_schema_and_table(schema, table);
    lua_pop(lua_state, 2); // "schema" and "table"

    if (new_expire_output.filename().empty() &&
        new_expire_output.table().empty()) {
        throw fmt_error(
            "Must set 'filename' and/or 'table' on expire output '{}'.",
            new_expire_output.name());
    }

    // required "maxzoom" field
    auto value = luaX_get_table_optional_uint32(
        lua_state, "maxzoom", -1, "The 'maxzoom' field in a expire output");
    if (value >= 1 && value <= 20) {
        new_expire_output.set_minzoom(value);
        new_expire_output.set_maxzoom(value);
    } else {
        throw std::runtime_error{
            "Value of 'maxzoom' field must be between 1 and 20."};
    }
    lua_pop(lua_state, 1); // "maxzoom"

    // optional "minzoom" field
    value = luaX_get_table_optional_uint32(
        lua_state, "minzoom", -1, "The 'minzoom' field in a expire output");
    if (value >= 1 && value <= new_expire_output.maxzoom()) {
        new_expire_output.set_minzoom(value);
    } else if (value != 0) {
        throw std::runtime_error{
            "Value of 'minzoom' field must be between 1 and 'maxzoom'."};
    }
    lua_pop(lua_state, 1); // "minzoom"

    return new_expire_output;
}

int setup_flex_expire_output(lua_State *lua_state,
                             std::vector<expire_output_t> *expire_outputs)
{
    if (lua_type(lua_state, 1) != LUA_TTABLE) {
        throw std::runtime_error{
            "Argument #1 to 'define_expire_output' must be a Lua table."};
    }

    create_expire_output(lua_state, expire_outputs);

    void *ptr = lua_newuserdata(lua_state, sizeof(std::size_t));
    auto *num = new (ptr) std::size_t{};
    *num = expire_outputs->size() - 1;
    luaL_getmetatable(lua_state, osm2pgsql_expire_output_name);
    lua_setmetatable(lua_state, -2);

    return 1;
}
