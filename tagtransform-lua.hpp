#ifndef TAGTRANSFORM_LUA_H
#define TAGTRANSFORM_LUA_H

#include <string>

#include "tagtransform.hpp"

extern "C" {
#include <lua.h>
}

class lua_tagtransform_t : public tagtransform_t
{
public:
    lua_tagtransform_t(options_t const *options);
    ~lua_tagtransform_t();

    bool filter_tags(osmium::OSMObject const &o, int *polygon, int *roads,
                     export_list const &exlist, taglist_t &out_tags,
                     bool strict = false) override;

    unsigned filter_rel_member_tags(taglist_t const &rel_tags,
                                    multitaglist_t const &member_tags,
                                    rolelist_t const &member_roles,
                                    int *member_superseded, int *make_boundary,
                                    int *make_polygon, int *roads,
                                    export_list const &exlist,
                                    taglist_t &out_tags,
                                    bool allow_typeless = false) override;

private:
    void check_lua_function_exists(std::string const &func_name);

    lua_State *L;
    std::string m_node_func, m_way_func, m_rel_func, m_rel_mem_func;
    bool m_extra_attributes;
};

#endif // TAGTRANSFORM_LUA_H
