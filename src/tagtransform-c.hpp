#ifndef OSM2PGSQL_TAGTRANSFORM_C_HPP
#define OSM2PGSQL_TAGTRANSFORM_C_HPP

#include "taginfo-impl.hpp"
#include "tagtransform.hpp"

class c_tagtransform_t : public tagtransform_t
{
public:
    c_tagtransform_t(options_t const *options, export_list const &exlist);

    std::unique_ptr<tagtransform_t> clone() const override;

    bool filter_tags(osmium::OSMObject const &o, int *polygon, int *roads,
                     taglist_t &out_tags, bool strict = false) override;

    bool filter_rel_member_tags(taglist_t const &rel_tags,
                                osmium::memory::Buffer const &members,
                                rolelist_t const &member_roles,
                                int *make_boundary, int *make_polygon,
                                int *roads, taglist_t &out_tags,
                                bool allow_typeless = false) override;

private:
    bool check_key(std::vector<taginfo> const &infos, char const *k,
                   bool *filter, int *flags, bool strict);

    options_t const *m_options;
    export_list m_export_list;
};

#endif // OSM2PGSQL_TAGTRANSFORM_C_HPP
