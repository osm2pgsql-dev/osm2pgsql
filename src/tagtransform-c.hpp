#ifndef OSM2PGSQL_TAGTRANSFORM_C_HPP
#define OSM2PGSQL_TAGTRANSFORM_C_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "taginfo-impl.hpp"
#include "tagtransform.hpp"

class c_tagtransform_t : public tagtransform_t
{
public:
    c_tagtransform_t(options_t const *options, export_list const &exlist);

    std::unique_ptr<tagtransform_t> clone() const override;

    bool filter_tags(osmium::OSMObject const &o, bool *polygon, bool *roads,
                     taglist_t &out_tags) override;

    bool filter_rel_member_tags(taglist_t const &rel_tags,
                                osmium::memory::Buffer const &members,
                                rolelist_t const &member_roles,
                                bool *make_boundary, bool *make_polygon,
                                bool *roads, taglist_t &out_tags) override;

private:
    bool check_key(std::vector<taginfo> const &infos, char const *k,
                   bool *filter, unsigned int *flags);

    options_t const *m_options;
    export_list m_export_list;
};

#endif // OSM2PGSQL_TAGTRANSFORM_C_HPP
