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
#include "options.hpp"
#include "tagtransform-lua.hpp"

lua_tagtransform_t::lua_tagtransform_t(std::string const *tag_transform_script,
                                       bool extra_attributes)
: m_lua_file(tag_transform_script), m_extra_attributes(extra_attributes)
{
    L = luaL_newstate();
    luaL_openlibs(L);
    if (luaL_dofile(L, m_lua_file->c_str())) {
        throw std::runtime_error{
            "Lua tag transform style error: {}."_format(lua_tostring(L, -1))};
    }

    check_lua_function_exists(node_func);
    check_lua_function_exists(way_func);
    check_lua_function_exists(rel_func);
    check_lua_function_exists(rel_mem_func);
}

lua_tagtransform_t::~lua_tagtransform_t() { lua_close(L); }

std::unique_ptr<tagtransform_t> lua_tagtransform_t::clone() const
{
    return std::make_unique<lua_tagtransform_t>(m_lua_file, m_extra_attributes);
}

void lua_tagtransform_t::check_lua_function_exists(char const *func_name)
{
    lua_getglobal(L, func_name);
    if (!lua_isfunction(L, -1)) {
        throw std::runtime_error{
            "Tag transform style does not contain a function {}."_format(
                func_name)};
    }
    lua_pop(L, 1);
}

bool lua_tagtransform_t::filter_tags(osmium::OSMObject const &o, bool *polygon,
                                     bool *roads, taglist_t *out_tags)
{
    switch (o.type()) {
    case osmium::item_type::node:
        lua_getglobal(L, node_func);
        break;
    case osmium::item_type::way:
        lua_getglobal(L, way_func);
        break;
    case osmium::item_type::relation:
        lua_getglobal(L, rel_func);
        break;
    default:
        throw std::runtime_error{"Unknown OSM type."};
    }

    lua_newtable(L); /* key value table */

    lua_Integer sz = 0;
    for (auto const &t : o.tags()) {
        lua_pushstring(L, t.key());
        lua_pushstring(L, t.value());
        lua_rawset(L, -3);
        ++sz;
    }
    if (m_extra_attributes && o.version() > 0) {
        taglist_t tags;
        tags.add_attributes(o);
        for (auto const &t : tags) {
            lua_pushstring(L, t.key.c_str());
            lua_pushstring(L, t.value.c_str());
            lua_rawset(L, -3);
        }
        sz += tags.size();
    }

    lua_pushinteger(L, sz);

    if (lua_pcall(L, 2, (o.type() == osmium::item_type::way) ? 4 : 2, 0)) {
        /* lua function failed */
        throw std::runtime_error{"Failed to execute lua function for basic tag "
                                 "processing: {}."_format(lua_tostring(L, -1))};
    }

    if (o.type() == osmium::item_type::way) {
        if (roads) {
            *roads = (int)lua_tointeger(L, -1);
        }
        lua_pop(L, 1);
        if (polygon) {
            *polygon = (int)lua_tointeger(L, -1);
        }
        lua_pop(L, 1);
    }

    lua_pushnil(L);
    while (lua_next(L, -2) != 0) {
        char const *const key = lua_tostring(L, -2);
        char const *const value = lua_tostring(L, -1);
        if (key == nullptr) {
            int const ltype = lua_type(L, -2);
            throw std::runtime_error{
                "Basic tag processing returned NULL key. Possibly this is "
                "due an incorrect data type '{}'."_format(
                    lua_typename(L, ltype))};
        }
        if (value == nullptr) {
            int const ltype = lua_type(L, -1);
            throw std::runtime_error{
                "Basic tag processing returned NULL value. Possibly this "
                "is due an incorrect data type '{}'."_format(
                    lua_typename(L, ltype))};
        }
        out_tags->add_tag(key, value);
        lua_pop(L, 1);
    }

    bool const filter = lua_tointeger(L, -2);

    lua_pop(L, 2);

    return filter;
}

bool lua_tagtransform_t::filter_rel_member_tags(
    taglist_t const &rel_tags, osmium::memory::Buffer const &members,
    rolelist_t const &member_roles, bool *make_boundary, bool *make_polygon,
    bool *roads, taglist_t *out_tags)
{
    size_t const num_members = member_roles.size();
    lua_getglobal(L, rel_mem_func);

    lua_newtable(L); /* relations key value table */

    for (auto const &rel_tag : rel_tags) {
        lua_pushstring(L, rel_tag.key.c_str());
        lua_pushstring(L, rel_tag.value.c_str());
        lua_rawset(L, -3);
    }

    lua_newtable(L); /* member tags table */

    int idx = 1;
    for (auto const &w : members.select<osmium::Way>()) {
        lua_pushnumber(L, idx++);
        lua_newtable(L); /* member key value table */
        for (auto const &member_tag : w.tags()) {
            lua_pushstring(L, member_tag.key());
            lua_pushstring(L, member_tag.value());
            lua_rawset(L, -3);
        }
        lua_rawset(L, -3);
    }

    lua_newtable(L); /* member roles table */

    for (size_t i = 0; i < num_members; ++i) {
        lua_pushnumber(L, i + 1);
        lua_pushstring(L, member_roles[i]);
        lua_rawset(L, -3);
    }

    lua_pushnumber(L, num_members);

    if (lua_pcall(L, 4, 6, 0)) {
        /* lua function failed */
        throw std::runtime_error{
            "Failed to execute lua function for relation tag "
            "processing: {}."_format(lua_tostring(L, -1))};
    }

    *roads = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    *make_polygon = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    *make_boundary = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);

    // obsolete member superseded is ignored.
    lua_pop(L, 1);

    lua_pushnil(L);
    while (lua_next(L, -2) != 0) {
        char const *const key = lua_tostring(L, -2);
        char const *const value = lua_tostring(L, -1);
        out_tags->add_tag(key, value);
        lua_pop(L, 1);
    }
    lua_pop(L, 1);

    bool const filter = lua_tointeger(L, -1);

    lua_pop(L, 1);

    return filter;
}
