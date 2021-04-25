#ifndef OSM2PGSQL_TAGTRANSFORM_LUA_HPP
#define OSM2PGSQL_TAGTRANSFORM_LUA_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2021 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <string>

#include "tagtransform.hpp"

extern "C"
{
#include <lua.h>
}

class lua_tagtransform_t : public tagtransform_t
{
    lua_tagtransform_t(lua_tagtransform_t const &other) = default;

public:
    explicit lua_tagtransform_t(options_t const *options);
    ~lua_tagtransform_t();

    std::unique_ptr<tagtransform_t> clone() const override;

    bool filter_tags(osmium::OSMObject const &o, int *polygon, int *roads,
                     taglist_t &out_tags) override;

    bool filter_rel_member_tags(taglist_t const &rel_tags,
                                osmium::memory::Buffer const &members,
                                rolelist_t const &member_roles,
                                int *make_boundary, int *make_polygon,
                                int *roads, taglist_t &out_tags) override;

private:
    void open_style();
    void check_lua_function_exists(std::string const &func_name);

    lua_State *L;
    std::string m_node_func, m_way_func, m_rel_func, m_rel_mem_func;
    std::string m_lua_file;
    bool m_extra_attributes;
};

#endif // OSM2PGSQL_TAGTRANSFORM_LUA_HPP
