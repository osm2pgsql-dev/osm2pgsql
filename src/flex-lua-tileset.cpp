/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "flex-lua-tileset.hpp"
#include "flex-tileset.hpp"
#include "format.hpp"
#include "lua-utils.hpp"
#include "pgsql.hpp"
#include "util.hpp"

#include <lua.hpp>

static flex_tileset_t &
create_flex_tileset(lua_State *lua_state, std::vector<flex_tileset_t> *tilesets)
{
    std::string const tileset_name =
        luaX_get_table_string(lua_state, "name", -1, "The tileset");

    check_identifier(tileset_name, "tileset names");

    if (util::find_by_name(*tilesets, tileset_name)) {
        throw fmt_error("Tileset with name '{}' already exists.", tileset_name);
    }

    auto &new_tileset = tilesets->emplace_back(tileset_name);

    lua_pop(lua_state, 1); // "name"

    // optional "filename" field
    auto const *filename =
        luaX_get_table_string(lua_state, "filename", -1, "The tileset", "");
    new_tileset.set_filename(filename);
    lua_pop(lua_state, 1); // "filename"

    // optional "schema" and "table" fields
    auto const *schema =
        luaX_get_table_string(lua_state, "schema", -1, "The tileset", "");
    check_identifier(schema, "schema field");
    auto const *table =
        luaX_get_table_string(lua_state, "table", -2, "The tileset", "");
    check_identifier(table, "table field");
    new_tileset.set_schema_and_table(schema, table);
    lua_pop(lua_state, 2); // "schema" and "table"

    if (new_tileset.filename().empty() && new_tileset.table().empty()) {
        throw fmt_error("Must set 'filename' and/or 'table' on tileset '{}'.",
                        new_tileset.name());
    }

    // required "maxzoom" field
    auto value = luaX_get_table_optional_uint32(
        lua_state, "maxzoom", -1, "The 'maxzoom' field in a tileset");
    if (value >= 1 && value <= 20) {
        new_tileset.set_minzoom(value);
        new_tileset.set_maxzoom(value);
    } else {
        throw std::runtime_error{
            "Value of 'maxzoom' field must be between 1 and 20."};
    }
    lua_pop(lua_state, 1); // "maxzoom"

    // optional "minzoom" field
    value = luaX_get_table_optional_uint32(lua_state, "minzoom", -1,
                                           "The 'minzoom' field in a tileset");
    if (value >= 1 && value <= new_tileset.maxzoom()) {
        new_tileset.set_minzoom(value);
    } else if (value != 0) {
        throw std::runtime_error{
            "Value of 'minzoom' field must be between 1 and 'maxzoom'."};
    }
    lua_pop(lua_state, 1); // "minzoom"

    return new_tileset;
}

int setup_flex_tileset(lua_State *lua_state,
                       std::vector<flex_tileset_t> *tilesets)
{
    if (lua_type(lua_state, 1) != LUA_TTABLE) {
        throw std::runtime_error{
            "Argument #1 to 'define_tileset' must be a Lua table."};
    }

    create_flex_tileset(lua_state, tilesets);

    void *ptr = lua_newuserdata(lua_state, sizeof(std::size_t));
    auto *num = new (ptr) std::size_t{};
    *num = tilesets->size() - 1;
    luaL_getmetatable(lua_state, osm2pgsql_tileset_name);
    lua_setmetatable(lua_state, -2);

    return 1;
}
