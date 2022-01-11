#ifndef OSM2PGSQL_TAGTRANSFORM_HPP
#define OSM2PGSQL_TAGTRANSFORM_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <memory>

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

    virtual bool filter_tags(osmium::OSMObject const &o, bool *polygon,
                             bool *roads, taglist_t &out_tags) = 0;

    virtual bool filter_rel_member_tags(taglist_t const &rel_tags,
                                        osmium::memory::Buffer const &members,
                                        rolelist_t const &member_roles,
                                        bool *make_boundary, bool *make_polygon,
                                        bool *roads, taglist_t &out_tags) = 0;
};

#endif // OSM2PGSQL_TAGTRANSFORM_HPP
