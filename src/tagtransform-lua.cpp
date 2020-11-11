extern "C"
{
#include <lauxlib.h>
#include <lualib.h>
}

#include "format.hpp"
#include "options.hpp"
#include "tagtransform-lua.hpp"

lua_tagtransform_t::lua_tagtransform_t(options_t const *options)
: m_node_func(
      options->tag_transform_node_func.get_value_or("filter_tags_node")),
  m_way_func(options->tag_transform_way_func.get_value_or("filter_tags_way")),
  m_rel_func(
      options->tag_transform_rel_func.get_value_or("filter_basic_tags_rel")),
  m_rel_mem_func(options->tag_transform_rel_mem_func.get_value_or(
      "filter_tags_relation_member")),
  m_lua_file(options->tag_transform_script.get()),
  m_extra_attributes(options->extra_attributes)
{
    open_style();
}

void lua_tagtransform_t::open_style()
{
    L = luaL_newstate();
    luaL_openlibs(L);
    if (luaL_dofile(L, m_lua_file.c_str())) {
        throw std::runtime_error{
            "Lua tag transform style error: {}."_format(lua_tostring(L, -1))};
    }

    check_lua_function_exists(m_node_func);
    check_lua_function_exists(m_way_func);
    check_lua_function_exists(m_rel_func);
    check_lua_function_exists(m_rel_mem_func);
}

lua_tagtransform_t::~lua_tagtransform_t() { lua_close(L); }

std::unique_ptr<tagtransform_t> lua_tagtransform_t::clone() const
{
    auto c = std::unique_ptr<lua_tagtransform_t>(new lua_tagtransform_t(*this));
    c->open_style();

    return std::unique_ptr<tagtransform_t>(c.release());
}

void lua_tagtransform_t::check_lua_function_exists(std::string const &func_name)
{
    lua_getglobal(L, func_name.c_str());
    if (!lua_isfunction(L, -1)) {
        throw std::runtime_error{
            "Tag transform style does not contain a function {}."_format(
                func_name)};
    }
    lua_pop(L, 1);
}

bool lua_tagtransform_t::filter_tags(osmium::OSMObject const &o, int *polygon,
                                     int *roads, taglist_t &out_tags, bool)
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
        out_tags.add_tag(key, value);
        lua_pop(L, 1);
    }

    bool const filter = lua_tointeger(L, -2);

    lua_pop(L, 2);

    return filter;
}

bool lua_tagtransform_t::filter_rel_member_tags(
    taglist_t const &rel_tags, osmium::memory::Buffer const &members,
    rolelist_t const &member_roles, int *make_boundary, int *make_polygon,
    int *roads, taglist_t &out_tags, bool)
{
    size_t const num_members = member_roles.size();
    lua_getglobal(L, m_rel_mem_func.c_str());

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
        out_tags.add_tag(key, value);
        lua_pop(L, 1);
    }
    lua_pop(L, 1);

    bool const filter = lua_tointeger(L, -1);

    lua_pop(L, 1);

    return filter;
}
