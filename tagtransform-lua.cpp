extern "C" {
#include <lauxlib.h>
#include <lualib.h>
}

#include <boost/format.hpp>

#include "options.hpp"
#include "tagtransform.hpp"

lua_tagtransform_t::lua_tagtransform_t(options_t const *options)
: m_options(options), L(luaL_newstate()),
  m_node_func(
      options->tag_transform_node_func.get_value_or("filter_tags_node")),
  m_way_func(options->tag_transform_way_func.get_value_or("filter_tags_way")),
  m_rel_func(
      options->tag_transform_rel_func.get_value_or("filter_basic_tags_rel")),
  m_rel_mem_func(options->tag_transform_rel_mem_func.get_value_or(
      "filter_tags_relation_member"))
{
    luaL_openlibs(L);
    luaL_dofile(L, options->tag_transform_script->c_str());

    check_lua_function_exists(m_node_func);
    check_lua_function_exists(m_way_func);
    check_lua_function_exists(m_rel_func);
    check_lua_function_exists(m_rel_mem_func);
}

lua_tagtransform_t::~lua_tagtransform_t() { lua_close(L); }

void lua_tagtransform_t::check_lua_function_exists(const std::string &func_name)
{
    lua_getglobal(L, func_name.c_str());
    if (!lua_isfunction(L, -1)) {
        throw std::runtime_error(
            (boost::format(
                 "Tag transform style does not contain a function %1%") %
             func_name)
                .str());
    }
    lua_pop(L, 1);
}

bool lua_tagtransform_t::filter_tags(osmium::OSMObject const &o, int *polygon,
                                     int *roads, export_list const &,
                                     taglist_t &out_tags, bool)
{
    switch (o.type()) {
    case osmium::item_type::node:
        lua_getglobal(L, m_node_func.c_str());
        break;
    case osmium::item_type::way:
        lua_getglobal(L, m_way_func.c_str());
        break;
    case osmium::item_type::relation:
        lua_getglobal(L, m_rel_func.c_str());
        break;
    default:
        throw std::runtime_error("Unknown OSM type");
    }

    lua_newtable(L); /* key value table */

    lua_Integer sz = 0;
    for (auto const &t : o.tags()) {
        lua_pushstring(L, t.key());
        lua_pushstring(L, t.value());
        lua_rawset(L, -3);
        ++sz;
    }
    if (m_options->extra_attributes && o.version() > 0) {
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
        fprintf(stderr,
                "Failed to execute lua function for basic tag processing: %s\n",
                lua_tostring(L, -1));
        /* lua function failed */
        return 1;
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
        const char *key = lua_tostring(L, -2);
        const char *value = lua_tostring(L, -1);
        out_tags.emplace_back(key, value);
        lua_pop(L, 1);
    }

    bool filter = lua_tointeger(L, -2);

    lua_pop(L, 2);

    return filter;
}

unsigned lua_tagtransform_t::filter_rel_member_tags(
    taglist_t const &rel_tags, multitaglist_t const &members_tags,
    rolelist_t const &member_roles, int *member_superseded, int *make_boundary,
    int *make_polygon, int *roads, export_list const &, taglist_t &out_tags,
    bool)
{
    lua_getglobal(L, m_rel_mem_func.c_str());

    lua_newtable(L); /* relations key value table */

    for (const auto &rel_tag : rel_tags) {
        lua_pushstring(L, rel_tag.key.c_str());
        lua_pushstring(L, rel_tag.value.c_str());
        lua_rawset(L, -3);
    }

    lua_newtable(L); /* member tags table */

    int idx = 1;
    for (const auto &member_tags : members_tags) {
        lua_pushnumber(L, idx++);
        lua_newtable(L); /* member key value table */
        for (const auto &member_tag : member_tags) {
            lua_pushstring(L, member_tag.key.c_str());
            lua_pushstring(L, member_tag.value.c_str());
            lua_rawset(L, -3);
        }
        lua_rawset(L, -3);
    }

    lua_newtable(L); /* member roles table */

    for (size_t i = 0; i < member_roles.size(); i++) {
        lua_pushnumber(L, i + 1);
        lua_pushstring(L, member_roles[i]);
        lua_rawset(L, -3);
    }

    lua_pushnumber(L, member_roles.size());

    if (lua_pcall(L, 4, 6, 0)) {
        fprintf(
            stderr,
            "Failed to execute lua function for relation tag processing: %s\n",
            lua_tostring(L, -1));
        /* lua function failed */
        return 1;
    }

    *roads = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    *make_polygon = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    *make_boundary = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);

    lua_pushnil(L);
    for (size_t i = 0; i < members_tags.size(); i++) {
        if (lua_next(L, -2)) {
            member_superseded[i] = (int)lua_tointeger(L, -1);
            lua_pop(L, 1);
        } else {
            fprintf(stderr,
                    "Failed to read member_superseded from lua function\n");
        }
    }
    lua_pop(L, 2);

    lua_pushnil(L);
    while (lua_next(L, -2) != 0) {
        const char *key = lua_tostring(L, -2);
        const char *value = lua_tostring(L, -1);
        out_tags.push_back(tag_t(key, value));
        lua_pop(L, 1);
    }
    lua_pop(L, 1);

    unsigned filter = (unsigned)lua_tointeger(L, -1);

    lua_pop(L, 1);

    return filter;
}
