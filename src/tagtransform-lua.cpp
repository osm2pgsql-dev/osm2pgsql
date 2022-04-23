/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

extern "C"
{
#include <lauxlib.h>
#include <lualib.h>
}

#include "format.hpp"
#include "tagtransform-lua.hpp"

#include <stdexcept>

lua_tagtransform_t::lua_tagtransform_t(std::string const *tag_transform_script,
                                       bool extra_attributes)
: m_lua_file(tag_transform_script), m_extra_attributes(extra_attributes)
{
    m_lua_state.reset(luaL_newstate());
    luaL_openlibs(lua_state());
    if (luaL_dofile(lua_state(), m_lua_file->c_str())) {
        throw std::runtime_error{"Lua tag transform style error: {}."_format(
            lua_tostring(lua_state(), -1))};
    }

    check_lua_function_exists(node_func);
    check_lua_function_exists(way_func);
    check_lua_function_exists(rel_func);
    check_lua_function_exists(rel_mem_func);
}

std::unique_ptr<tagtransform_t> lua_tagtransform_t::clone() const
{
    return std::make_unique<lua_tagtransform_t>(m_lua_file, m_extra_attributes);
}

void lua_tagtransform_t::check_lua_function_exists(char const *func_name)
{
    lua_getglobal(lua_state(), func_name);
    if (!lua_isfunction(lua_state(), -1)) {
        throw std::runtime_error{
            "Tag transform style does not contain a function {}."_format(
                func_name)};
    }
    lua_pop(lua_state(), 1);
}

/**
 * Read tags from the Lua table on the stack and write them to out_tags
 */
static void get_out_tags(lua_State *lua_state, taglist_t *out_tags)
{
    lua_pushnil(lua_state);
    while (lua_next(lua_state, -2) != 0) {
        auto const key_type = lua_type(lua_state, -2);
        // They key must be a string, otherwise the lua_tostring() function
        // below will change it to a string and the lua_next() iteration will
        // break.
        if (key_type != LUA_TSTRING) {
            throw std::runtime_error{
                "Basic tag processing found incorrect data type"
                "'{}', use a string."_format(
                    lua_typename(lua_state, key_type))};
        }

        auto const value_type = lua_type(lua_state, -1);
        // They key must be a string or number (which will automatically be
        // converted to a string).
        if (value_type != LUA_TSTRING && value_type != LUA_TNUMBER) {
            throw std::runtime_error{
                "Basic tag processing found incorrect data type"
                "'{}', use a string."_format(
                    lua_typename(lua_state, value_type))};
        }

        char const *const key = lua_tostring(lua_state, -2);
        char const *const value = lua_tostring(lua_state, -1);
        out_tags->add_tag(key, value);
        lua_pop(lua_state, 1);
    }
    lua_pop(lua_state, 1);
}

bool lua_tagtransform_t::filter_tags(osmium::OSMObject const &o, bool *polygon,
                                     bool *roads, taglist_t *out_tags)
{
    switch (o.type()) {
    case osmium::item_type::node:
        lua_getglobal(lua_state(), node_func);
        break;
    case osmium::item_type::way:
        lua_getglobal(lua_state(), way_func);
        break;
    case osmium::item_type::relation:
        lua_getglobal(lua_state(), rel_func);
        break;
    default:
        throw std::runtime_error{"Unknown OSM type."};
    }

    lua_newtable(lua_state()); /* key value table */

    lua_Integer sz = 0;
    for (auto const &t : o.tags()) {
        lua_pushstring(lua_state(), t.key());
        lua_pushstring(lua_state(), t.value());
        lua_rawset(lua_state(), -3);
        ++sz;
    }
    if (m_extra_attributes && o.version() > 0) {
        taglist_t tags;
        tags.add_attributes(o);
        for (auto const &t : tags) {
            lua_pushstring(lua_state(), t.key.c_str());
            lua_pushstring(lua_state(), t.value.c_str());
            lua_rawset(lua_state(), -3);
        }
        sz += tags.size();
    }

    lua_pushinteger(lua_state(), sz);

    if (lua_pcall(lua_state(), 2, (o.type() == osmium::item_type::way) ? 4 : 2,
                  0)) {
        /* lua function failed */
        throw std::runtime_error{
            "Failed to execute lua function for basic tag "
            "processing: {}."_format(lua_tostring(lua_state(), -1))};
    }

    if (o.type() == osmium::item_type::way) {
        if (roads) {
            *roads = (int)lua_tointeger(lua_state(), -1);
        }
        lua_pop(lua_state(), 1);
        if (polygon) {
            *polygon = (int)lua_tointeger(lua_state(), -1);
        }
        lua_pop(lua_state(), 1);
    }

    get_out_tags(lua_state(), out_tags);

    bool const filter = lua_tointeger(lua_state(), -1);
    lua_pop(lua_state(), 1);

    return filter;
}

bool lua_tagtransform_t::filter_rel_member_tags(
    taglist_t const &rel_tags, osmium::memory::Buffer const &members,
    rolelist_t const &member_roles, bool *make_boundary, bool *make_polygon,
    bool *roads, taglist_t *out_tags)
{
    size_t const num_members = member_roles.size();
    lua_getglobal(lua_state(), rel_mem_func);

    lua_newtable(lua_state()); /* relations key value table */

    for (auto const &rel_tag : rel_tags) {
        lua_pushstring(lua_state(), rel_tag.key.c_str());
        lua_pushstring(lua_state(), rel_tag.value.c_str());
        lua_rawset(lua_state(), -3);
    }

    lua_newtable(lua_state()); /* member tags table */

    int idx = 1;
    for (auto const &w : members.select<osmium::Way>()) {
        lua_pushnumber(lua_state(), idx++);
        lua_newtable(lua_state()); /* member key value table */
        for (auto const &member_tag : w.tags()) {
            lua_pushstring(lua_state(), member_tag.key());
            lua_pushstring(lua_state(), member_tag.value());
            lua_rawset(lua_state(), -3);
        }
        lua_rawset(lua_state(), -3);
    }

    lua_newtable(lua_state()); /* member roles table */

    for (size_t i = 0; i < num_members; ++i) {
        lua_pushnumber(lua_state(), i + 1);
        lua_pushstring(lua_state(), member_roles[i]);
        lua_rawset(lua_state(), -3);
    }

    lua_pushnumber(lua_state(), num_members);

    if (lua_pcall(lua_state(), 4, 6, 0)) {
        /* lua function failed */
        throw std::runtime_error{
            "Failed to execute lua function for relation tag "
            "processing: {}."_format(lua_tostring(lua_state(), -1))};
    }

    *roads = (int)lua_tointeger(lua_state(), -1);
    lua_pop(lua_state(), 1);
    *make_polygon = (int)lua_tointeger(lua_state(), -1);
    lua_pop(lua_state(), 1);
    *make_boundary = (int)lua_tointeger(lua_state(), -1);
    lua_pop(lua_state(), 1);

    // obsolete member superseded is ignored.
    lua_pop(lua_state(), 1);

    get_out_tags(lua_state(), out_tags);

    bool const filter = lua_tointeger(lua_state(), -1);
    lua_pop(lua_state(), 1);

    return filter;
}
