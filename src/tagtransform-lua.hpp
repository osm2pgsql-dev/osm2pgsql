#ifndef OSM2PGSQL_TAGTRANSFORM_LUA_HPP
#define OSM2PGSQL_TAGTRANSFORM_LUA_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
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
public:
    explicit lua_tagtransform_t(std::string const *tag_transform_script,
                                bool extra_attributes);

    ~lua_tagtransform_t() noexcept override = default;

    std::unique_ptr<tagtransform_t> clone() const override;

    bool filter_tags(osmium::OSMObject const &o, bool *polygon, bool *roads,
                     taglist_t *out_tags) override;

    bool filter_rel_member_tags(taglist_t const &rel_tags,
                                osmium::memory::Buffer const &members,
                                rolelist_t const &member_roles,
                                bool *make_boundary, bool *make_polygon,
                                bool *roads, taglist_t *out_tags) override;

private:
    constexpr static char const *const node_func = "filter_tags_node";
    constexpr static char const *const way_func = "filter_tags_way";
    constexpr static char const *const rel_func = "filter_basic_tags_rel";
    constexpr static char const *const rel_mem_func =
        "filter_tags_relation_member";

    void check_lua_function_exists(char const *func_name);

    struct lua_state_deleter_t
    {
        void operator()(lua_State *state) const noexcept { lua_close(state); }
    };

    lua_State *lua_state() noexcept { return m_lua_state.get(); }

    std::unique_ptr<lua_State, lua_state_deleter_t> m_lua_state;

    std::string const *m_lua_file;
    bool m_extra_attributes;
};

#endif // OSM2PGSQL_TAGTRANSFORM_LUA_HPP
