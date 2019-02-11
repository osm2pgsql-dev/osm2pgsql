#include <boost/algorithm/string/predicate.hpp>
#include <cstdlib>
#include <cstring>

#include "options.hpp"
#include "taginfo_impl.hpp"
#include "tagtransform-c.hpp"
#include "wildcmp.hpp"

namespace {

static const struct
{
    int offset;
    const char *highway;
    int roads;
} layers[] = {{1, "proposed", 0},       {2, "construction", 0},
              {10, "steps", 0},         {10, "cycleway", 0},
              {10, "bridleway", 0},     {10, "footway", 0},
              {10, "path", 0},          {11, "track", 0},
              {15, "service", 0},

              {24, "tertiary_link", 0}, {25, "secondary_link", 1},
              {27, "primary_link", 1},  {28, "trunk_link", 1},
              {29, "motorway_link", 1},

              {30, "raceway", 0},       {31, "pedestrian", 0},
              {32, "living_street", 0}, {33, "road", 0},
              {33, "unclassified", 0},  {33, "residential", 0},
              {34, "tertiary", 0},      {36, "secondary", 1},
              {37, "primary", 1},       {38, "trunk", 1},
              {39, "motorway", 1}};

static const unsigned int nLayers = (sizeof(layers) / sizeof(*layers));

void add_z_order(taglist_t &tags, int *roads)
{
    const std::string *layer = tags.get("layer");
    const std::string *highway = tags.get("highway");
    bool bridge = tags.get_bool("bridge", false);
    bool tunnel = tags.get_bool("tunnel", false);
    const std::string *railway = tags.get("railway");
    const std::string *boundary = tags.get("boundary");

    int z_order = 0;

    int l = layer ? (int)strtol(layer->c_str(), NULL, 10) : 0;
    z_order = 100 * l;
    *roads = 0;

    if (highway) {
        for (unsigned i = 0; i < nLayers; i++) {
            if (!strcmp(layers[i].highway, highway->c_str())) {
                z_order += layers[i].offset;
                *roads = layers[i].roads;
                break;
            }
        }
    }

    if (railway && !railway->empty()) {
        z_order += 35;
        *roads = 1;
    }
    /* Administrative boundaries are rendered at low zooms so we prefer to use the roads table */
    if (boundary && *boundary == "administrative")
        *roads = 1;

    if (bridge)
        z_order += 100;

    if (tunnel)
        z_order -= 100;

    char z[13];
    snprintf(z, sizeof(z), "%d", z_order);
    tags.push_back(tag_t("z_order", z));
}

} // anonymous namespace

c_tagtransform_t::c_tagtransform_t(options_t const *options,
                                   export_list const &exlist)
: m_options(options), m_export_list(exlist)
{
}

std::unique_ptr<tagtransform_t> c_tagtransform_t::clone() const
{
    return std::unique_ptr<tagtransform_t>(
        new c_tagtransform_t(m_options, m_export_list));
}

bool c_tagtransform_t::check_key(std::vector<taginfo> const &infos,
                                 char const *k, bool *filter, int *flags,
                                 bool strict)
{
    //go through the actual tags found on the item and keep the ones in the export list
    size_t i = 0;
    for (; i < infos.size(); i++) {
        const taginfo &info = infos[i];
        if (info.flags & FLAG_DELETE) {
            if (wildMatch(info.name.c_str(), k)) {
                return false;
            }
        } else if (strcmp(info.name.c_str(), k) == 0) {
            *filter = false;
            *flags |= info.flags;

            return true;
        }
    }

    // if we didn't find any tags that we wanted to export
    // and we aren't strictly adhering to the list
    if (!strict) {
        if (m_options->hstore_mode != HSTORE_NONE) {
            /* ... but if hstore_match_only is set then don't take this
                 as a reason for keeping the object */
            if (!m_options->hstore_match_only)
                *filter = false;
            /* with hstore, copy all tags... */
            return true;
        } else if (m_options->hstore_columns.size() > 0) {
            /* does this column match any of the hstore column prefixes? */
            size_t j = 0;
            for (; j < m_options->hstore_columns.size(); ++j) {
                if (boost::starts_with(k, m_options->hstore_columns[j])) {
                    /* ... but if hstore_match_only is set then don't take this
                         as a reason for keeping the object */
                    if (!m_options->hstore_match_only) {
                        *filter = false;
                    }
                    return true;
                }
            }
        }
    }

    return false;
}

bool c_tagtransform_t::filter_tags(osmium::OSMObject const &o, int *polygon,
                                   int *roads, taglist_t &out_tags, bool strict)
{
    //assume we dont like this set of tags
    bool filter = true;

    int flags = 0;
    int add_area_tag = 0;

    auto export_type = o.type();
    if (o.type() == osmium::item_type::relation) {
        export_type = osmium::item_type::way;
    }
    const std::vector<taginfo> &infos = m_export_list.get(export_type);

    /* We used to only go far enough to determine if it's a polygon or not,
       but now we go through and filter stuff we don't need
       pop each tag off and keep it in the temp list if we like it */
    for (auto const &item : o.tags()) {
        char const *k = item.key();
        char const *v = item.value();
        //if we want to do more than the export list says
        if (!strict) {
            if (o.type() == osmium::item_type::relation &&
                strcmp("type", k) == 0) {
                out_tags.emplace_back(k, v);
                continue;
            }
            /* Allow named islands to appear as polygons */
            if (strcmp("natural", k) == 0 && strcmp("coastline", v) == 0) {
                add_area_tag = 1;

                /* Discard natural=coastline tags (we render these from a shapefile instead) */
                if (!m_options->keep_coastlines) {
                    continue;
                }
            }
        }

        //go through the actual tags found on the item and keep the ones in the export list
        if (check_key(infos, k, &filter, &flags, strict)) {
            out_tags.emplace_back(k, v);
        }
    }
    if (m_options->extra_attributes && o.version() > 0) {
        out_tags.add_attributes(o);
    }

    if (polygon) {
        if (add_area_tag) {
            /* If we need to force this as a polygon, append an area tag */
            out_tags.push_dedupe(tag_t("area", "yes"));
            *polygon = 1;
        } else {
            auto const *area = o.tags()["area"];
            if (area)
                *polygon = taglist_t::value_to_bool(area, flags & FLAG_POLYGON);
            else
                *polygon = flags & FLAG_POLYGON;
        }
    }

    if (roads && !filter && (o.type() == osmium::item_type::way)) {
        add_z_order(out_tags, roads);
    }

    return filter;
}

bool c_tagtransform_t::filter_rel_member_tags(
    taglist_t const &rel_tags, osmium::memory::Buffer const &,
    rolelist_t const &, int *make_boundary, int *make_polygon,
    int *roads, taglist_t &out_tags, bool allow_typeless)
{
    //if it has a relation figure out what kind it is
    const std::string *type = rel_tags.get("type");
    bool is_route = false, is_boundary = false, is_multipolygon = false;
    if (type) {
        //what kind of relation is it
        if (*type == "route")
            is_route = true;
        else if (*type == "boundary")
            is_boundary = true;
        else if (*type == "multipolygon")
            is_multipolygon = true;
        else if (!allow_typeless)
            return true;
    } //you didnt have a type and it was required
    else if (!allow_typeless) {
        return true;
    }

    /* Clone tags from relation */
    for (const auto &rel_tag : rel_tags) {
        //copy the name tag as "route_name"
        if (is_route && (rel_tag.key == "name"))
            out_tags.push_dedupe(tag_t("route_name", rel_tag.value));
        //copy all other tags except for "type"
        if (rel_tag.key != "type")
            out_tags.push_dedupe(rel_tag);
    }

    if (out_tags.empty()) {
        return true;
    }

    if (is_route) {
        const std::string *netw = rel_tags.get("network");
        int networknr = -1;

        if (netw != nullptr) {
            const std::string *state = rel_tags.get("state");
            std::string statetype("yes");
            if (state) {
                if (*state == "alternate")
                    statetype = "alternate";
                else if (*state == "connection")
                    statetype = "connection";
            }
            if (*netw == "lcn") {
                networknr = 10;
                out_tags.push_dedupe(tag_t("lcn", statetype));
            } else if (*netw == "rcn") {
                networknr = 11;
                out_tags.push_dedupe(tag_t("rcn", statetype));
            } else if (*netw == "ncn") {
                networknr = 12;
                out_tags.push_dedupe(tag_t("ncn", statetype));
            } else if (*netw == "lwn") {
                networknr = 20;
                out_tags.push_dedupe(tag_t("lwn", statetype));
            } else if (*netw == "rwn") {
                networknr = 21;
                out_tags.push_dedupe(tag_t("rwn", statetype));
            } else if (*netw == "nwn") {
                networknr = 22;
                out_tags.push_dedupe(tag_t("nwn", statetype));
            }
        }

        const std::string *prefcol = rel_tags.get("preferred_color");
        if (prefcol != NULL && prefcol->size() == 1) {
            if ((*prefcol)[0] == '0' || (*prefcol)[0] == '1' ||
                (*prefcol)[0] == '2' || (*prefcol)[0] == '3' ||
                (*prefcol)[0] == '4') {
                out_tags.push_dedupe(tag_t("route_pref_color", *prefcol));
            } else {
                out_tags.push_dedupe(tag_t("route_pref_color", "0"));
            }
        } else {
            out_tags.push_dedupe(tag_t("route_pref_color", "0"));
        }

        const std::string *relref = rel_tags.get("ref");
        if (relref != NULL) {
            if (networknr == 10) {
                out_tags.push_dedupe(tag_t("lcn_ref", *relref));
            } else if (networknr == 11) {
                out_tags.push_dedupe(tag_t("rcn_ref", *relref));
            } else if (networknr == 12) {
                out_tags.push_dedupe(tag_t("ncn_ref", *relref));
            } else if (networknr == 20) {
                out_tags.push_dedupe(tag_t("lwn_ref", *relref));
            } else if (networknr == 21) {
                out_tags.push_dedupe(tag_t("rwn_ref", *relref));
            } else if (networknr == 22) {
                out_tags.push_dedupe(tag_t("nwn_ref", *relref));
            }
        }
    } else if (is_boundary) {
        /* Boundaries will get converted into multiple geometries:
         - Linear features will end up in the line and roads tables (useful for admin boundaries)
         - Polygon features also go into the polygon table (useful for national_forests)
         The edges of the polygon also get treated as linear fetaures allowing these to be rendered seperately. */
        *make_boundary = 1;
    } else if (is_multipolygon && out_tags.contains("boundary")) {
        /* Treat type=multipolygon exactly like type=boundary if it has a boundary tag. */
        *make_boundary = 1;
    } else if (is_multipolygon) {
        *make_polygon = 1;
    }

    add_z_order(out_tags, roads);

    return false;
}
