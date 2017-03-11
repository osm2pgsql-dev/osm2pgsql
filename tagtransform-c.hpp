#ifndef TAGTRANSFORM_C_H
#define TAGTRANSFORM_C_H

#include "tagtransform.hpp"

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

#endif // TAGTRANSFORM_C_H
