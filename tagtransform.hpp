#ifndef TAGTRANSFORM_H
#define TAGTRANSFORM_H

#include "osmtypes.hpp"

#include <string>

struct options_t;
struct export_list;

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

#endif //TAGTRANSFORM_H
