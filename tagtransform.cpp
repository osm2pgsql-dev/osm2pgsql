#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <vector>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/format.hpp>

#include "tagtransform.hpp"
#include "options.hpp"
#include "config.h"
#include "wildcmp.hpp"
#include "taginfo_impl.hpp"

#ifdef HAVE_LUA
extern "C" {
    #include <lualib.h>
    #include <lauxlib.h>
}
#endif


static const struct {
    int offset;
    const char *highway;
    int roads;
} layers[] = {
    { 1, "proposed", 0 },
    { 2, "construction", 0 },
    { 10, "steps", 0 },
    { 10, "cycleway", 0 },
    { 10, "bridleway", 0 },
    { 10, "footway", 0 },
    { 10, "path", 0 },
    { 11, "track", 0 },
    { 15, "service", 0 },

    { 24, "tertiary_link", 0 },
    { 25, "secondary_link",1 },
    { 27, "primary_link",  1 },
    { 28, "trunk_link",    1 },
    { 29, "motorway_link", 1 },

    { 30, "raceway",       0 },
    { 31, "pedestrian",    0 },
    { 32, "living_street", 0 },
    { 33, "road",          0 },
    { 33, "unclassified",  0 },
    { 33, "residential",   0 },
    { 34, "tertiary",      0 },
    { 36, "secondary",     1 },
    { 37, "primary",       1 },
    { 38, "trunk",         1 },
    { 39, "motorway",      1 }
};

static const unsigned int nLayers = (sizeof(layers)/sizeof(*layers));

namespace {
void add_z_order(taglist_t &tags, int *roads)
{
    const std::string *layer = tags.get("layer");
    const std::string *highway = tags.get("highway");
    bool bridge = tags.get_bool("bridge", false);
    bool tunnel = tags.get_bool("tunnel", false);
    const std::string *railway = tags.get("railway");
    const std::string *boundary = tags.get("boundary");

    int z_order = 0;

    int l = layer ? strtol(layer->c_str(), NULL, 10) : 0;
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

unsigned int c_filter_rel_member_tags(const taglist_t &rel_tags,
        const multitaglist_t &member_tags, const rolelist_t &member_roles,
        int *member_superseeded, int *make_boundary, int *make_polygon, int *roads,
        const export_list &exlist, taglist_t &out_tags, bool allow_typeless)
{
    //if it has a relation figure out what kind it is
    const std::string *type = rel_tags.get("type");
    bool is_route = false, is_boundary = false, is_multipolygon = false;
    if (type)
    {
        //what kind of relation is it
        if (*type == "route")
            is_route = true;
        else if (*type == "boundary")
            is_boundary = true;
        else if (*type == "multipolygon")
            is_multipolygon = true;
    }//you didnt have a type and it was required
    else if (!allow_typeless)
    {
        return 1;
    }

    /* Clone tags from relation */
    for (const auto& rel_tag: rel_tags) {
        //copy the name tag as "route_name"
        if (is_route && (rel_tag.key == "name"))
            out_tags.push_dedupe(tag_t("route_name", rel_tag.value));
        //copy all other tags except for "type"
        if (rel_tag.key != "type")
            out_tags.push_dedupe(rel_tag);
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
            if ((*prefcol)[0] == '0' || (*prefcol)[0] == '1'
                    || (*prefcol)[0] == '2' || (*prefcol)[0] == '3'
                    || (*prefcol)[0] == '4') {
                out_tags.push_dedupe(tag_t("route_pref_color", *prefcol));
            } else {
                out_tags.push_dedupe(tag_t("route_pref_color", "0"));
            }
        } else {
            out_tags.push_dedupe(tag_t("route_pref_color", "0"));
        }

        const std::string *relref = rel_tags.get("ref");
        if (relref != NULL ) {
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

        /* Collect a list of polygon-like tags, these are used later to
         identify if an inner rings looks like it should be rendered separately */
        taglist_t poly_tags;
        for (const auto& tag: out_tags) {
            if (tag.key == "area") {
                poly_tags.push_back(tag);
            } else {
                const std::vector<taginfo> &infos = exlist.get(osmium::item_type::way);
                for (const auto& info: infos) {
                    if (info.name == tag.key) {
                        if (info.flags & FLAG_POLYGON) {
                            poly_tags.push_back(tag);
                        }
                        break;
                    }
                }
            }
        }

        /* Copy the tags from the outer way(s) if the relation is untagged (with
         * respect to tags that influence its polygon nature. Tags like name or fixme should be fine*/
        if (poly_tags.empty()) {
            int first_outerway = 1;
            for (size_t i = 0; i < member_tags.size(); i++) {
                if (member_roles[i] && strcmp(member_roles[i], "inner") == 0)
                    continue;

                /* insert all tags of the first outerway to the potential list of copied tags. */
                if (first_outerway) {
                    for (const auto& tag: member_tags[i]) {
                        poly_tags.push_back(tag);
                    }
                } else {
                    /* Check if all of the tags in the list of potential tags are present on this way,
                       otherwise remove from the list of potential tags. Tags need to be present on
                       all outer ways to be copied over to the relation */
                    taglist_t::iterator it = poly_tags.begin();
                    while (it != poly_tags.end()) {
                        if (!member_tags[i].contains(it->key))
                            /* This tag is not present on all member outer ways, so don't copy it over to relation */
                            it = poly_tags.erase(it);
                        else
                            ++it;
                    }
                }
                first_outerway = 0;
            }
            /* Copy the list identified outer way tags over to the relation */
            for (const auto& poly_tag: poly_tags) {
                out_tags.push_dedupe(poly_tag);
            }

            /* We need to re-check and only keep polygon tags in the list of polytags */
            // TODO what is that for? The list is cleared just below.
            taglist_t::iterator q = poly_tags.begin();
            const std::vector<taginfo> &infos = exlist.get(osmium::item_type::way);
            while (q != poly_tags.end()) {
                bool contains_tag = false;
                for (std::vector<taginfo>::const_iterator info = infos.begin();
                     info != infos.end(); ++info) {
                    if (info->name == q->key) {
                        if (info->flags & FLAG_POLYGON) {
                            contains_tag = true;
                        }
                        break;
                    }
                }

                if (contains_tag)
                    ++q;
                else
                    q = poly_tags.erase(q);
            }
        }
    } else if(!allow_typeless) {
        /* Unknown type, just exit */
        out_tags.clear();
        return 1;
    }

    if (out_tags.empty()) {
        return 1;
    }

    /* If we are creating a multipolygon then we
     mark each member so that we can skip them during iterate_ways
     but only if the polygon-tags look the same as the outer ring */
    if (make_polygon) {
        for (size_t i = 0; i < member_tags.size(); i++) {
            member_superseeded[i] = 1;
            for (const auto& member_tag: member_tags[i]) {
                const std::string *v = out_tags.get(member_tag.key);
                if (!v || *v != member_tag.value) {
                    /* z_order and osm_ are automatically generated tags, so ignore them */
                    if ((member_tag.key != "z_order") && (member_tag.key != "osm_user") &&
                        (member_tag.key != "osm_version") && (member_tag.key != "osm_uid") &&
                        (member_tag.key != "osm_changeset") && (member_tag.key != "osm_timestamp")) {
                        member_superseeded[i] = 0;
                        break;
                    }
                }
            }
        }
    }

    add_z_order(out_tags, roads);

    return 0;
}
} // anonymous namespace

#ifdef HAVE_LUA
unsigned tagtransform::lua_filter_rel_member_tags(const taglist_t &rel_tags,
        const multitaglist_t &members_tags, const rolelist_t &member_roles,
        int *member_superseeded, int *make_boundary, int *make_polygon, int *roads,
        taglist_t &out_tags)
{
    lua_getglobal(L, m_rel_mem_func.c_str());

    lua_newtable(L);    /* relations key value table */

    for (const auto& rel_tag: rel_tags) {
        lua_pushstring(L, rel_tag.key.c_str());
        lua_pushstring(L, rel_tag.value.c_str());
        lua_rawset(L, -3);
    }

    lua_newtable(L);    /* member tags table */

    int idx = 1;
    for (const auto& member_tags: members_tags) {
        lua_pushnumber(L, idx++);
        lua_newtable(L);    /* member key value table */
        for (const auto& member_tag: member_tags) {
            lua_pushstring(L, member_tag.key.c_str());
            lua_pushstring(L, member_tag.value.c_str());
            lua_rawset(L, -3);
        }
        lua_rawset(L, -3);
    }

    lua_newtable(L);    /* member roles table */

    for (size_t i = 0; i < member_roles.size(); i++) {
        lua_pushnumber(L, i + 1);
        lua_pushstring(L, member_roles[i]);
        lua_rawset(L, -3);
    }

    lua_pushnumber(L, member_roles.size());

    if (lua_pcall(L,4,6,0)) {
        fprintf(stderr, "Failed to execute lua function for relation tag processing: %s\n", lua_tostring(L, -1));
        /* lua function failed */
        return 1;
    }

    *roads = lua_tointeger(L, -1);
    lua_pop(L,1);
    *make_polygon = lua_tointeger(L, -1);
    lua_pop(L,1);
    *make_boundary = lua_tointeger(L,-1);
    lua_pop(L,1);

    lua_pushnil(L);
    for (size_t i = 0; i < members_tags.size(); i++) {
        if (lua_next(L,-2)) {
            member_superseeded[i] = lua_tointeger(L,-1);
            lua_pop(L,1);
        } else {
            fprintf(stderr, "Failed to read member_superseeded from lua function\n");
        }
    }
    lua_pop(L,2);

    lua_pushnil(L);
    while (lua_next(L,-2) != 0) {
        const char *key = lua_tostring(L,-2);
        const char *value = lua_tostring(L,-1);
        out_tags.push_back(tag_t(key, value));
        lua_pop(L,1);
    }
    lua_pop(L,1);

    int filter = lua_tointeger(L, -1);

    lua_pop(L,1);

    return filter;
}

void tagtransform::check_lua_function_exists(const std::string &func_name)
{
    lua_getglobal(L, func_name.c_str());
    if (!lua_isfunction (L, -1)) {
        throw std::runtime_error((boost::format("Tag transform style does not contain a function %1%")
                                  % func_name).str());
    }
    lua_pop(L,1);
}
#endif

tagtransform::tagtransform(const options_t *options_)
    : options(options_), transform_method(options_->tag_transform_script)
#ifdef HAVE_LUA
    , L(NULL)
    , m_node_func(   options->tag_transform_node_func.   get_value_or("filter_tags_node"))
    , m_way_func(    options->tag_transform_way_func.    get_value_or("filter_tags_way"))
    , m_rel_func(    options->tag_transform_rel_func.    get_value_or("filter_basic_tags_rel"))
    , m_rel_mem_func(options->tag_transform_rel_mem_func.get_value_or("filter_tags_relation_member"))
#endif /* HAVE_LUA */
{
    if (transform_method) {
        fprintf(stderr, "Using lua based tag processing pipeline with script %s\n", options->tag_transform_script->c_str());
#ifdef HAVE_LUA
        L = luaL_newstate();
        luaL_openlibs(L);
        luaL_dofile(L, options->tag_transform_script->c_str());

        check_lua_function_exists(m_node_func);
        check_lua_function_exists(m_way_func);
        check_lua_function_exists(m_rel_func);
        check_lua_function_exists(m_rel_mem_func);
#else
        throw std::runtime_error("Error: Could not init lua tag transform, as lua support was not compiled into this version");
#endif
    } else {
        fprintf(stderr, "Using built-in tag processing pipeline\n");
    }
}

tagtransform::~tagtransform() {
#ifdef HAVE_LUA
    if (transform_method) {
        lua_close(L);
    }
#endif
}

bool tagtransform::filter_tags(osmium::OSMObject const &o,
                               int *polygon, int *roads, const export_list &exlist,
                               taglist_t &out_tags, bool strict)
{
    if (transform_method) {
        return lua_filter_basic_tags(o, polygon, roads, out_tags);
    }

    return c_filter_basic_tags(o, polygon, roads, exlist,
                               out_tags, strict);
}

unsigned tagtransform::filter_rel_member_tags(const taglist_t &rel_tags,
        const multitaglist_t &member_tags, const rolelist_t &member_roles,
        int *member_superseeded, int *make_boundary, int *make_polygon, int *roads,
        const export_list &exlist, taglist_t &out_tags, bool allow_typeless)
{
    if (transform_method) {
#ifdef HAVE_LUA
        return lua_filter_rel_member_tags(rel_tags, member_tags, member_roles, member_superseeded, make_boundary, make_polygon, roads, out_tags);
#else
        return 1;
#endif
    } else {
        return c_filter_rel_member_tags(rel_tags, member_tags, member_roles, member_superseeded, make_boundary, make_polygon, roads, exlist, out_tags, allow_typeless);
    }
}

bool tagtransform::lua_filter_basic_tags(osmium::OSMObject const &o,
                                         int *polygon, int *roads, taglist_t &out_tags)
{
#ifdef HAVE_LUA
    switch (o.type()) {
        case osmium::item_type::node:
            lua_getglobal(L, m_node_func.c_str());
            break;
        case osmium::item_type::way:
            lua_getglobal(L, m_way_func.c_str());
            break;
        case osmium::item_type::relation:
            lua_getglobal(L, m_rel_func.c_str());
            break;
        default:
            throw std::runtime_error("Unknown OSM type");
    }

    lua_newtable(L);    /* key value table */

    lua_Integer sz = 0;
    for (auto const &t: o.tags()) {
        lua_pushstring(L, t.key());
        lua_pushstring(L, t.value());
        lua_rawset(L, -3);
        ++sz;
    }
    if (options->extra_attributes && o.version() > 0) {
        taglist_t tags;
        tags.add_attributes(o);
        for (auto const &t: tags) {
            lua_pushstring(L, t.key.c_str());
            lua_pushstring(L, t.value.c_str());
            lua_rawset(L, -3);
        }
        sz += tags.size();
    }

    lua_pushinteger(L, sz);

    if (lua_pcall(L, 2, (o.type() == osmium::item_type::way) ? 4 : 2, 0)) {
        fprintf(stderr, "Failed to execute lua function for basic tag processing: %s\n", lua_tostring(L, -1));
        /* lua function failed */
        return 1;
    }

    if (o.type() == osmium::item_type::way) {
        if (roads) {
            *roads = lua_tointeger(L, -1);
        }
        lua_pop(L,1);
        if (polygon) {
            *polygon = lua_tointeger(L, -1);
        }
        lua_pop(L,1);
    }

    lua_pushnil(L);
    while (lua_next(L,-2) != 0) {
        const char *key = lua_tostring(L,-2);
        const char *value = lua_tostring(L,-1);
        out_tags.emplace_back(key, value);
        lua_pop(L,1);
    }

    bool filter = lua_tointeger(L, -2);

    lua_pop(L,2);

    return filter;
#else
    return true;
#endif
}

/* Go through the given tags and determine the union of flags. Also remove
 * any tags from the list that we don't know about */
bool tagtransform::c_filter_basic_tags(osmium::OSMObject const &o, int *polygon,
                                       int *roads, const export_list &exlist,
                                       taglist_t &out_tags, bool strict)
{
    //assume we dont like this set of tags
    bool filter = true;

    int flags = 0;
    int add_area_tag = 0;

    auto export_type = o.type();
    if (o.type() == osmium::item_type::relation) {
        export_type = osmium::item_type::way;
    }
    const std::vector<taginfo> &infos = exlist.get(export_type);

    /* We used to only go far enough to determine if it's a polygon or not,
       but now we go through and filter stuff we don't need
       pop each tag off and keep it in the temp list if we like it */
    for (auto const &item : o.tags()) {
        char const *k = item.key();
        char const *v = item.value();
        //if we want to do more than the export list says
        if(!strict) {
            if (o.type() == osmium::item_type::relation && strcmp("type", k) == 0) {
                out_tags.emplace_back(k, v);
                filter = false;
                continue;
            }
            /* Allow named islands to appear as polygons */
            if (strcmp("natural", k) == 0 && strcmp("coastline", v) == 0) {
                add_area_tag = 1;

                /* Discard natural=coastline tags (we render these from a shapefile instead) */
                if (!options->keep_coastlines) {
                    continue;
                }
            }
        }

        //go through the actual tags found on the item and keep the ones in the export list
        size_t i = 0;
        for (; i < infos.size(); i++) {
            const taginfo &info = infos[i];
            if (info.flags & FLAG_DELETE) {
                if (wildMatch(info.name.c_str(), k)) {
                    break;
                }
            } else if (strcmp(info.name.c_str(), k) == 0) {
                filter = false;
                flags |= info.flags;

                out_tags.emplace_back(k, v);
                break;
            }
        }

        // if we didn't find any tags that we wanted to export
        // and we aren't strictly adhering to the list
        if (i == infos.size() && !strict) {
            if (options->hstore_mode != HSTORE_NONE) {
                /* with hstore, copy all tags... */
                out_tags.emplace_back(k, v);
                /* ... but if hstore_match_only is set then don't take this
                 as a reason for keeping the object */
                if (!options->hstore_match_only)
                    filter = false;
            } else if (options->hstore_columns.size() > 0) {
                /* does this column match any of the hstore column prefixes? */
                size_t j = 0;
                for(; j < options->hstore_columns.size(); ++j) {
                    if (boost::starts_with(k, options->hstore_columns[j])) {
                        out_tags.emplace_back(k, v);
                        /* ... but if hstore_match_only is set then don't take this
                         as a reason for keeping the object */
                        if (!options->hstore_match_only) {
                            filter = false;
                        }
                        break;
                    }
                }
            }
        }
    }
    if (options->extra_attributes && o.version() > 0) {
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
