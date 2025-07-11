/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2025 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "flex-lua-expire-output.hpp"

#include "expire-output.hpp"
#include "format.hpp"
#include "lua-utils.hpp"

#include <lua.hpp>

namespace {

expire_output_t &
create_expire_output(lua_State *lua_state, std::string const &default_schema,
                     std::vector<expire_output_t> *expire_outputs)
{
    auto &new_expire_output = expire_outputs->emplace_back();

    // optional "filename" field
    auto const *filename = luaX_get_table_string(lua_state, "filename", -1,
                                                 "The expire output", "");
    new_expire_output.set_filename(filename);
    lua_pop(lua_state, 1); // "filename"

    // optional "schema" and "table" fields
    auto const *schema = luaX_get_table_string(
        lua_state, "schema", -1, "The expire output", default_schema.c_str());
    check_identifier(schema, "schema field");
    auto const *table =
        luaX_get_table_string(lua_state, "table", -2, "The expire output", "");
    check_identifier(table, "table field");
    new_expire_output.set_schema_and_table(schema, table);
    lua_pop(lua_state, 2); // "schema" and "table"

    if (new_expire_output.filename().empty() &&
        new_expire_output.table().empty()) {
        throw std::runtime_error{
            "Must set 'filename' and/or 'table' on expire output."};
    }

    // required "maxzoom" field
    auto const maxzoom = luaX_get_table_optional_uint32(
        lua_state, "maxzoom", -1, "The 'maxzoom' field in a expire output", 1,
        20, "1 and 20");
    new_expire_output.set_minzoom(maxzoom);
    new_expire_output.set_maxzoom(maxzoom);
    lua_pop(lua_state, 1); // "maxzoom"

    // optional "minzoom" field
    auto const minzoom = luaX_get_table_optional_uint32(
        lua_state, "minzoom", -1, "The 'minzoom' field in a expire output", 1,
        maxzoom, "1 and 'maxzoom'");
    if (minzoom > 0) {
        new_expire_output.set_minzoom(minzoom);
    }
    lua_pop(lua_state, 1); // "minzoom"

    return new_expire_output;
}

TRAMPOLINE_WRAPPED_OBJECT(expire_output, tostring)
TRAMPOLINE_WRAPPED_OBJECT(expire_output, filename)
TRAMPOLINE_WRAPPED_OBJECT(expire_output, maxzoom)
TRAMPOLINE_WRAPPED_OBJECT(expire_output, minzoom)
TRAMPOLINE_WRAPPED_OBJECT(expire_output, schema)
TRAMPOLINE_WRAPPED_OBJECT(expire_output, table)

} // anonymous namespace

int setup_flex_expire_output(lua_State *lua_state,
                             std::string const &default_schema,
                             std::vector<expire_output_t> *expire_outputs)
{
    if (lua_type(lua_state, 1) != LUA_TTABLE) {
        throw std::runtime_error{
            "Argument #1 to 'define_expire_output' must be a Lua table."};
    }

    create_expire_output(lua_state, default_schema, expire_outputs);

    void *ptr = lua_newuserdata(lua_state, sizeof(std::size_t));
    auto *num = new (ptr) std::size_t{};
    *num = expire_outputs->size() - 1;
    luaL_getmetatable(lua_state, OSM2PGSQL_EXPIRE_OUTPUT_CLASS);
    lua_setmetatable(lua_state, -2);

    return 1;
}

/**
 * Define the osm2pgsql.ExpireOutput class/metatable.
 */
void lua_wrapper_expire_output::init(lua_State *lua_state)
{
    luaX_set_up_metatable(
        lua_state, "ExpireOutput", OSM2PGSQL_EXPIRE_OUTPUT_CLASS,
        {{"__tostring", lua_trampoline_expire_output_tostring},
         {"filename", lua_trampoline_expire_output_filename},
         {"maxzoom", lua_trampoline_expire_output_maxzoom},
         {"minzoom", lua_trampoline_expire_output_minzoom},
         {"schema", lua_trampoline_expire_output_schema},
         {"table", lua_trampoline_expire_output_table}});
}

int lua_wrapper_expire_output::tostring() const
{
    std::string const str =
        fmt::format("osm2pgsql.ExpireOutput[minzoom={},maxzoom={},filename={},"
                    "schema={},table={}]",
                    self().minzoom(), self().maxzoom(), self().filename(),
                    self().schema(), self().table());
    luaX_pushstring(lua_state(), str);

    return 1;
}

int lua_wrapper_expire_output::filename() const noexcept
{
    luaX_pushstring(lua_state(), self().filename());
    return 1;
}

int lua_wrapper_expire_output::maxzoom() const noexcept
{
    lua_pushinteger(lua_state(), self().maxzoom());
    return 1;
}

int lua_wrapper_expire_output::minzoom() const noexcept
{
    lua_pushinteger(lua_state(), self().minzoom());
    return 1;
}

int lua_wrapper_expire_output::schema() const noexcept
{
    luaX_pushstring(lua_state(), self().schema());
    return 1;
}

int lua_wrapper_expire_output::table() const noexcept
{
    luaX_pushstring(lua_state(), self().table());
    return 1;
}
