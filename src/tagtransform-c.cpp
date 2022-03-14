/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <cstdlib>
#include <cstring>

#include "options.hpp"
#include "taginfo-impl.hpp"
#include "tagtransform-c.hpp"
#include "util.hpp"
#include "wildcmp.hpp"

namespace {

static const struct
{
    char const *highway;
    int offset;
    bool roads;
} layers[] = {{"proposed", 1, false},       {"construction", 2, false},
              {"steps", 10, false},         {"cycleway", 10, false},
              {"bridleway", 10, false},     {"footway", 10, false},
              {"path", 10, false},          {"track", 11, false},
              {"service", 15, false},

              {"tertiary_link", 24, false}, {"secondary_link", 25, true},
              {"primary_link", 27, true},   {"trunk_link", 28, true},
              {"motorway_link", 29, true},

              {"raceway", 30, false},       {"pedestrian", 31, false},
              {"living_street", 32, false}, {"road", 33, false},
              {"unclassified", 33, false},  {"residential", 33, false},
              {"tertiary", 34, false},      {"secondary", 36, true},
              {"primary", 37, true},        {"trunk", 38, true},
              {"motorway", 39, true}};

void add_z_order(taglist_t *tags, bool *roads)
{
    std::string const *const layer = tags->get("layer");
    std::string const *const highway = tags->get("highway");
    bool const bridge = tags->get_bool("bridge", false);
    bool const tunnel = tags->get_bool("tunnel", false);
    std::string const *const railway = tags->get("railway");
    std::string const *const boundary = tags->get("boundary");

    int z_order = 0;

    int l = layer ? (int)strtol(layer->c_str(), nullptr, 10) : 0;
    z_order = 100 * l;
    *roads = false;

    if (highway) {
        for (const auto &layer : layers) {
            if (*highway == layer.highway) {
                z_order += layer.offset;
                *roads = layer.roads;
                break;
            }
        }
    }

    if (railway && !railway->empty()) {
        z_order += 35;
        *roads = true;
    }
    /* Administrative boundaries are rendered at low zooms so we prefer to use the roads table */
    if (boundary && *boundary == "administrative") {
        *roads = true;
    }

    if (bridge) {
        z_order += 100;
    }

    if (tunnel) {
        z_order -= 100;
    }

    util::integer_to_buffer z{z_order};
    tags->add_tag("z_order", z.c_str());
}

} // anonymous namespace

c_tagtransform_t::c_tagtransform_t(options_t const *options, export_list exlist)
: m_options(options), m_export_list(std::move(exlist))
{}

std::unique_ptr<tagtransform_t> c_tagtransform_t::clone() const
{
    return std::make_unique<c_tagtransform_t>(m_options, m_export_list);
}

static bool starts_with(char const *input, std::string const& test) noexcept
{
    return std::strncmp(input, test.c_str(), test.size()) == 0;
}

bool c_tagtransform_t::check_key(std::vector<taginfo> const &infos,
                                 char const *k, bool *filter,
                                 unsigned int *flags)
{
    //go through the actual tags found on the item and keep the ones in the export list
    for (auto const &info : infos) {
        if (info.flags & FLAG_DELETE) {
            if (wildMatch(info.name.c_str(), k)) {
                return false;
            }
        } else if (std::strcmp(info.name.c_str(), k) == 0) {
            *filter = false;
            *flags |= info.flags;

            return true;
        }
    }

    // if we didn't find any tags that we wanted to export
    if (m_options->hstore_mode != hstore_column::none) {
        /* ... but if hstore_match_only is set then don't take this
                 as a reason for keeping the object */
        if (!m_options->hstore_match_only) {
            *filter = false;
        }
        /* with hstore, copy all tags... */
        return true;
    }

    if (!m_options->hstore_columns.empty()) {
        /* does this column match any of the hstore column prefixes? */
        for (auto const &column : m_options->hstore_columns) {
            if (starts_with(k, column)) {
                /* ... but if hstore_match_only is set then don't take this
                         as a reason for keeping the object */
                if (!m_options->hstore_match_only) {
                    *filter = false;
                }
                return true;
            }
        }
    }

    return false;
}

bool c_tagtransform_t::filter_tags(osmium::OSMObject const &o, bool *polygon,
                                   bool *roads, taglist_t *out_tags)
{
    //assume we dont like this set of tags
    bool filter = true;

    unsigned int flags = 0;
    int add_area_tag = 0;

    auto export_type = o.type();
    if (o.type() == osmium::item_type::relation) {
        export_type = osmium::item_type::way;
    }
    auto const &infos = m_export_list.get(export_type);

    /* We used to only go far enough to determine if it's a polygon or not,
       but now we go through and filter stuff we don't need
       pop each tag off and keep it in the temp list if we like it */
    for (auto const &item : o.tags()) {
        char const *const k = item.key();
        char const *const v = item.value();

        if (o.type() == osmium::item_type::relation &&
            std::strcmp("type", k) == 0) {
            out_tags->add_tag(k, v);
            continue;
        }
        /* Allow named islands to appear as polygons */
        if (std::strcmp("natural", k) == 0 &&
            std::strcmp("coastline", v) == 0) {
            add_area_tag = 1;

            /* Discard natural=coastline tags (we render these from a shapefile instead) */
            if (!m_options->keep_coastlines) {
                continue;
            }
        }

        //go through the actual tags found on the item and keep the ones in the export list
        if (check_key(infos, k, &filter, &flags)) {
            out_tags->add_tag(k, v);
        }
    }
    if (m_options->extra_attributes && o.version() > 0) {
        out_tags->add_attributes(o);
    }

    if (polygon) {
        if (add_area_tag) {
            /* If we need to force this as a polygon, append an area tag */
            out_tags->add_tag_if_not_exists("area", "yes");
            *polygon = true;
        } else {
            auto const *area = o.tags()["area"];
            if (area) {
                *polygon = taglist_t::value_to_bool(area, flags & FLAG_POLYGON);
            } else {
                *polygon = flags & FLAG_POLYGON;
            }
        }
    }

    if (roads && !filter && (o.type() == osmium::item_type::way)) {
        add_z_order(out_tags, roads);
    }

    return filter;
}

bool c_tagtransform_t::filter_rel_member_tags(
    taglist_t const &rel_tags, osmium::memory::Buffer const &,
    rolelist_t const &, bool *make_boundary, bool *make_polygon, bool *roads,
    taglist_t *out_tags)
{
    std::string const *type = rel_tags.get("type");
    if (!type) {
        return true;
    }

    bool is_route = false;
    bool is_boundary = false;
    bool is_multipolygon = false;

    if (*type == "route") {
        is_route = true;
    } else if (*type == "boundary") {
        is_boundary = true;
    } else if (*type == "multipolygon") {
        is_multipolygon = true;
    } else {
        return true;
    }

    /* Clone tags from relation */
    for (auto const &rel_tag : rel_tags) {
        //copy the name tag as "route_name"
        if (is_route && (rel_tag.key == "name")) {
            out_tags->add_tag_if_not_exists("route_name", rel_tag.value);
        }
        //copy all other tags except for "type"
        if (rel_tag.key != "type") {
            out_tags->add_tag_if_not_exists(rel_tag);
        }
    }

    if (out_tags->empty()) {
        return true;
    }

    if (is_route) {
        std::string const *netw = rel_tags.get("network");
        int networknr = -1;

        if (netw != nullptr) {
            std::string const *state = rel_tags.get("state");
            std::string statetype("yes");
            if (state) {
                if (*state == "alternate") {
                    statetype = "alternate";
                } else if (*state == "connection") {
                    statetype = "connection";
                }
            }
            if (*netw == "lcn") {
                networknr = 10;
                out_tags->add_tag_if_not_exists("lcn", statetype);
            } else if (*netw == "rcn") {
                networknr = 11;
                out_tags->add_tag_if_not_exists("rcn", statetype);
            } else if (*netw == "ncn") {
                networknr = 12;
                out_tags->add_tag_if_not_exists("ncn", statetype);
            } else if (*netw == "lwn") {
                networknr = 20;
                out_tags->add_tag_if_not_exists("lwn", statetype);
            } else if (*netw == "rwn") {
                networknr = 21;
                out_tags->add_tag_if_not_exists("rwn", statetype);
            } else if (*netw == "nwn") {
                networknr = 22;
                out_tags->add_tag_if_not_exists("nwn", statetype);
            }
        }

        std::string const *prefcol = rel_tags.get("preferred_color");
        if (prefcol != nullptr && prefcol->size() == 1) {
            if ((*prefcol)[0] == '0' || (*prefcol)[0] == '1' ||
                (*prefcol)[0] == '2' || (*prefcol)[0] == '3' ||
                (*prefcol)[0] == '4') {
                out_tags->add_tag_if_not_exists("route_pref_color", *prefcol);
            } else {
                out_tags->add_tag_if_not_exists("route_pref_color", "0");
            }
        } else {
            out_tags->add_tag_if_not_exists("route_pref_color", "0");
        }

        std::string const *relref = rel_tags.get("ref");
        if (relref != nullptr) {
            if (networknr == 10) {
                out_tags->add_tag_if_not_exists("lcn_ref", *relref);
            } else if (networknr == 11) {
                out_tags->add_tag_if_not_exists("rcn_ref", *relref);
            } else if (networknr == 12) {
                out_tags->add_tag_if_not_exists("ncn_ref", *relref);
            } else if (networknr == 20) {
                out_tags->add_tag_if_not_exists("lwn_ref", *relref);
            } else if (networknr == 21) {
                out_tags->add_tag_if_not_exists("rwn_ref", *relref);
            } else if (networknr == 22) {
                out_tags->add_tag_if_not_exists("nwn_ref", *relref);
            }
        }
    } else if (is_boundary ||
               (is_multipolygon && out_tags->contains("boundary"))) {
        /* Boundaries will get converted into multiple geometries:
         - Linear features will end up in the line and roads tables (useful for admin boundaries)
         - Polygon features also go into the polygon table (useful for national_forests)
         The edges of the polygon also get treated as linear fetaures allowing these to be rendered seperately. */
        *make_boundary = true;
    } else if (is_multipolygon) {
        *make_polygon = true;
    }

    add_z_order(out_tags, roads);

    return false;
}
