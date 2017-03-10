
#ifndef TAGTRANSFORM_H
#define TAGTRANSFORM_H

#include "config.h"
#include "osmtypes.hpp"

#include <string>

struct options_t;
struct export_list;

#ifdef HAVE_LUA
extern "C" {
    #include <lua.h>
}
#endif

class tagtransform_t
{
public:
    static std::unique_ptr<tagtransform_t>
    make_tagtransform(options_t const *options);

    virtual ~tagtransform_t() = 0;

    virtual bool filter_tags(osmium::OSMObject const &o, int *polygon,
                             int *roads, export_list const &exlist,
                             taglist_t &out_tags, bool strict = false) = 0;

    virtual unsigned filter_rel_member_tags(
        taglist_t const &rel_tags, multitaglist_t const &member_tags,
        rolelist_t const &member_roles, int *member_superseded,
        int *make_boundary, int *make_polygon, int *roads,
        export_list const &exlist, taglist_t &out_tags,
        bool allow_typeless = false) = 0;
};

class c_tagtransform_t : public tagtransform_t
{
public:
    c_tagtransform_t(options_t const *options);

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
    options_t const *m_options;
};

#ifdef HAVE_LUA
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

    options_t const *m_options;
    lua_State *L;
    std::string m_node_func, m_way_func, m_rel_func, m_rel_mem_func;
};
#endif

#endif //TAGTRANSFORM_H
