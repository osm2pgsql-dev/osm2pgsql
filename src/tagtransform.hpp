#ifndef OSM2PGSQL_TAGTRANSFORM_HPP
#define OSM2PGSQL_TAGTRANSFORM_HPP

#include <string>

#include <osmium/memory/buffer.hpp>

#include "osmtypes.hpp"

class export_list;
class options_t;

class tagtransform_t
{
public:
    static std::unique_ptr<tagtransform_t>
    make_tagtransform(options_t const *options, export_list const &exlist);

    virtual ~tagtransform_t() = 0;

    virtual std::unique_ptr<tagtransform_t> clone() const = 0;

    virtual bool filter_tags(osmium::OSMObject const &o, int *polygon,
                             int *roads, taglist_t &out_tags,
                             bool strict = false) = 0;

    virtual bool filter_rel_member_tags(taglist_t const &rel_tags,
                                        osmium::memory::Buffer const &members,
                                        rolelist_t const &member_roles,
                                        int *make_boundary, int *make_polygon,
                                        int *roads, taglist_t &out_tags,
                                        bool allow_typeless = false) = 0;
};

#endif // OSM2PGSQL_TAGTRANSFORM_HPP
