
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdexcept>
#include "osmtypes.hpp"
#include "keyvals.hpp"
#include "tagtransform.hpp"
#include "output-pgsql.hpp"
#include "options.hpp"
#include "config.h"
#include "wildcmp.hpp"
#include "taginfo_impl.hpp"


static const struct {
    int offset;
    const char *highway;
    int roads;
} layers[] = {
    { 3, "minor",         0 },
    { 3, "road",          0 },
    { 3, "unclassified",  0 },
    { 3, "residential",   0 },
    { 4, "tertiary_link", 0 },
    { 4, "tertiary",      0 },
    { 6, "secondary_link",1 },
    { 6, "secondary",     1 },
    { 7, "primary_link",  1 },
    { 7, "primary",       1 },
    { 8, "trunk_link",    1 },
    { 8, "trunk",         1 },
    { 9, "motorway_link", 1 },
    { 9, "motorway",      1 }
};

static const unsigned int nLayers = (sizeof(layers)/sizeof(*layers));

namespace {
int add_z_order(keyval *tags, int *roads) {
    const std::string *layer = tags->getItem("layer");
    const std::string *highway = tags->getItem("highway");
    const std::string *bridge = tags->getItem("bridge");
    const std::string *tunnel = tags->getItem("tunnel");
    const std::string *railway = tags->getItem("railway");
    const std::string *boundary = tags->getItem("boundary");

    int z_order = 0;
    char z[13];

    int l = layer ? strtol(layer->c_str(), NULL, 10) : 0;
    z_order = 10 * l;
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
        z_order += 5;
        *roads = 1;
    }
    /* Administrative boundaries are rendered at low zooms so we prefer to use the roads table */
    if (boundary && *boundary == "administrative")
        *roads = 1;

    if (bridge  && (*bridge == "true" || *bridge == "yes" || *bridge == "1"))
        z_order += 10;

    if (tunnel && (*tunnel == "true" || *tunnel == "yes" || *tunnel == "1"))
        z_order -= 10;

    snprintf(z, sizeof(z), "%d", z_order);
    tags->addItem("z_order", z, 0);

    return 0;
}

unsigned int c_filter_rel_member_tags(
        keyval *rel_tags, const int member_count,
        keyval *member_tags, const char * const *member_roles,
        int * member_superseeded, int * make_boundary, int * make_polygon, int * roads,
        const export_list *exlist, bool allow_typeless) {

    struct keyval tags, *q, poly_tags;
    int first_outerway, contains_tag;

    //if it has a relation figure out what kind it is
    const std::string *type = rel_tags->getItem("type");
    bool is_route = false, is_boundary = false, is_multipolygon = false;
    if (type)
    {
        //what kind of relation is it
        is_route = *type == "route";
        is_boundary = *type == "boundary";
        is_multipolygon = *type == "multipolygon";
    }//you didnt have a type and it was required
    else if (!allow_typeless)
    {
        return 1;
    }

    /* Clone tags from relation */
    for (keyval *p = rel_tags->firstItem(); p; p = rel_tags->nextItem(p)) {
        //copy the name tag as "route_name"
        if (is_route && (p->key == "name"))
            tags.addItem("route_name", p->value, true);
        //copy all other tags except for "type"
        else if (p->key != "type")
            tags.addItem(p->key, p->value, true);
    }

    if (is_route) {
        const std::string *netw = rel_tags->getItem("network");
        int networknr = -1;

        if (netw != NULL) {
            const std::string *state = rel_tags->getItem("state");
            std::string statetype("yes");
            if (state) {
                if (*state == "alternate")
                    statetype = "alternate";
                else if (*state == "connection")
                    statetype = "connection";
            }
            if (*netw == "lcn") {
                networknr = 10;
                tags.addItem("lcn", statetype, true);
            } else if (*netw == "rcn") {
                networknr = 11;
                tags.addItem("rcn", statetype, true);
            } else if (*netw == "ncn") {
                networknr = 12;
                tags.addItem("ncn", statetype, true);
            } else if (*netw == "lwn") {
                networknr = 20;
                tags.addItem("lwn", statetype, true);
            } else if (*netw == "rwn") {
                networknr = 21;
                tags.addItem("rwn", statetype, true);
            } else if (*netw == "nwn") {
                networknr = 22;
                tags.addItem("nwn", statetype, true);
            }
        }

        const std::string *prefcol = rel_tags->getItem("preferred_color");
        if (prefcol != NULL && prefcol->size() == 1) {
            if ((*prefcol)[0] == '0' || (*prefcol)[0] == '1'
                    || (*prefcol)[0] == '2' || (*prefcol)[0] == '3'
                    || (*prefcol)[0] == '4') {
                tags.addItem("route_pref_color", *prefcol, true);
            } else {
                tags.addItem("route_pref_color", "0", true);
            }
        } else {
            tags.addItem("route_pref_color", "0", true);
        }

        const std::string *relref = rel_tags->getItem("ref");
        if (relref != NULL ) {
            if (networknr == 10) {
                tags.addItem("lcn_ref", *relref, true);
            } else if (networknr == 11) {
                tags.addItem("rcn_ref", *relref, true);
            } else if (networknr == 12) {
                tags.addItem("ncn_ref", *relref, true);
            } else if (networknr == 20) {
                tags.addItem("lwn_ref", *relref, true);
            } else if (networknr == 21) {
                tags.addItem("rwn_ref", *relref, true);
            } else if (networknr == 22) {
                tags.addItem("nwn_ref", *relref, true);
            }
        }
    } else if (is_boundary) {
        /* Boundaries will get converted into multiple geometries:
         - Linear features will end up in the line and roads tables (useful for admin boundaries)
         - Polygon features also go into the polygon table (useful for national_forests)
         The edges of the polygon also get treated as linear fetaures allowing these to be rendered seperately. */
        *make_boundary = 1;
    } else if (is_multipolygon && tags.getItem("boundary")) {
        /* Treat type=multipolygon exactly like type=boundary if it has a boundary tag. */
        *make_boundary = 1;
    } else if (is_multipolygon) {
        *make_polygon = 1;

        /* Collect a list of polygon-like tags, these are used later to
         identify if an inner rings looks like it should be rendered separately */
        for (keyval *p = tags.firstItem(); p; p = tags.nextItem(p)) {
            if (p->key == "area") {
                poly_tags.addItem(p->key, p->value, true);
            } else {
                const std::vector<taginfo> &infos = exlist->get(OSMTYPE_WAY);
                for (unsigned i = 0; i < infos.size(); i++) {
                    const taginfo &info = infos[i];
                    if (info.name == p->key) {
                        if (info.flags & FLAG_POLYGON) {
                            poly_tags.addItem(p->key, p->value, true);
                        }
                        break;
                    }
                }
            }
        }

        /* Copy the tags from the outer way(s) if the relation is untagged (with
         * respect to tags that influence its polygon nature. Tags like name or fixme should be fine*/
        if (!poly_tags.listHasData()) {
            first_outerway = 1;
            for (int i = 0; i < member_count; i++) {
                if (member_roles[i] && !strcmp(member_roles[i], "inner"))
                    continue;

                /* insert all tags of the first outerway to the potential list of copied tags. */
                if (first_outerway) {
                    for (keyval *p = member_tags[i].firstItem(); p; p = member_tags[i].nextItem(p))
                        poly_tags.addItem(p->key, p->value, true);
                } else {
                    /* Check if all of the tags in the list of potential tags are present on this way,
                       otherwise remove from the list of potential tags. Tags need to be present on
                       all outer ways to be copied over to the relation */
                    q = poly_tags.firstItem();
                    while (q) {
                        const keyval *p = member_tags[i].getTag(q->key);
                        if ((p != NULL) && (p->value == q->value)) {
                            q = poly_tags.nextItem(q);
                        } else {
                            /* This tag is not present on all member outer ways, so don't copy it over to relation */
                            keyval *qq = poly_tags.nextItem(q);
                            q->removeTag();
                            q = qq;
                        }
                    }
                }
                first_outerway = 0;
            }
            /* Copy the list identified outer way tags over to the relation */
            for (q = poly_tags.firstItem(); q; q = poly_tags.nextItem(q))
                tags.addItem(q->key, q->value, true);

            /* We need to re-check and only keep polygon tags in the list of polytags */
            q = poly_tags.firstItem();
            while (q) {
                contains_tag = 0;
                const std::vector<taginfo> &infos = exlist->get(OSMTYPE_WAY);
                for (unsigned j = 0; j < infos.size(); j++) {
                    const taginfo &info = infos[j];
                    if (info.name == q->key) {
                        if (info.flags & FLAG_POLYGON) {
                            contains_tag = 1;
                            break;
                        }
                    }
                }
                if (contains_tag == 0) {
                    keyval *qq = poly_tags.nextItem(q);
                    q->removeTag();
                    q = qq;
                } else {
                    q = poly_tags.nextItem(q);
                }
            }
        }
        poly_tags.resetList();
    } else if(!allow_typeless) {
        /* Unknown type, just exit */
        tags.resetList();
        poly_tags.resetList();
        return 1;
    }

    if (!tags.listHasData()) {
        tags.resetList();
        poly_tags.resetList();
        return 1;
    }

    /* If we are creating a multipolygon then we
     mark each member so that we can skip them during iterate_ways
     but only if the polygon-tags look the same as the outer ring */
    if (make_polygon) {
        for (int i = 0; i < member_count; i++) {
            int match = 1;
            for (const keyval *p = member_tags[i].firstItem(); p; p = member_tags[i].nextItem(p)) {
                const std::string *v = tags.getItem(p->key);
                if (!v || *v != p->value) {
                    /* z_order and osm_ are automatically generated tags, so ignore them */
                    if ((p->key != "z_order") && (p->key != "osm_user") &&
                        (p->key != "osm_version") && (p->key != "osm_uid") &&
                        (p->key != "osm_changeset") && (p->key != "osm_timestamp")) {
                        match = 0;
                        break;
                    }
                }
            }
            if (match) {
                member_superseeded[i] = 1;
            } else {
                member_superseeded[i] = 0;
            }
        }
    }

    tags.moveList(rel_tags);

    add_z_order(rel_tags, roads);

    return 0;
}

#ifdef HAVE_LUA
unsigned int lua_filter_rel_member_tags(lua_State* L, const char* rel_mem_func, keyval *rel_tags, const int member_count,
        keyval *member_tags,const char * const * member_roles,
        int * member_superseeded, int * make_boundary, int * make_polygon, int * roads) {

    int i;
    int filter;
    int count = 0;
    struct keyval *item;
    const char * key, * value;

    lua_getglobal(L, rel_mem_func);

    lua_newtable(L);    /* relations key value table */

    while( (item = rel_tags->popItem()) != NULL ) {
        lua_pushstring(L, item->key.c_str());
        lua_pushstring(L, item->value.c_str());
        lua_rawset(L, -3);
        delete(item);
        count++;
    }

    lua_newtable(L);    /* member tags table */

    for (i = 1; i <= member_count; i++) {
        lua_pushnumber(L, i);
        lua_newtable(L);    /* member key value table */
        while( (item = member_tags[i - 1].popItem()) != NULL ) {
            lua_pushstring(L, item->key.c_str());
            lua_pushstring(L, item->value.c_str());
            lua_rawset(L, -3);
            delete(item);
            count++;
        }
        lua_rawset(L, -3);
    }

    lua_newtable(L);    /* member roles table */

    for (i = 0; i < member_count; i++) {
        lua_pushnumber(L, i + 1);
        lua_pushstring(L, member_roles[i]);
        lua_rawset(L, -3);
    }

    lua_pushnumber(L, member_count);

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
    for (i = 0; i < member_count; i++) {
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
        key = lua_tostring(L,-2);
        value = lua_tostring(L,-1);
        rel_tags->addItem(key, value, false);
        lua_pop(L,1);
    }
    lua_pop(L,1);

    filter = lua_tointeger(L, -1);

    lua_pop(L,1);

    return filter;
}

void check_lua_function_exists(lua_State *L, const std::string &func_name) {
    lua_getglobal(L, func_name.c_str());
    if (!lua_isfunction (L, -1)) {
        throw std::runtime_error((boost::format("Tag transform style does not contain a function %1%")
                                  % func_name).str());
    }
    lua_pop(L,1);
}
#endif
} // anonymous namespace

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

                check_lua_function_exists(L, m_node_func);
                check_lua_function_exists(L, m_way_func);
                check_lua_function_exists(L, m_rel_func);
                check_lua_function_exists(L, m_rel_mem_func);
#else
		throw std::runtime_error("Error: Could not init lua tag transform, as lua support was not compiled into this version");
#endif
	} else {
		fprintf(stderr, "Using built-in tag processing pipeline\n");
	}
}

tagtransform::~tagtransform() {
#ifdef HAVE_LUA
	if (transform_method)
    lua_close(L);
#endif
}

unsigned int tagtransform::filter_node_tags(struct keyval *tags, const export_list *exlist, bool strict) {
    int poly, roads;
    if (transform_method) {
        return lua_filter_basic_tags(OSMTYPE_NODE, tags, &poly, &roads);
    } else {
        return c_filter_basic_tags(OSMTYPE_NODE, tags, &poly, &roads, exlist, strict);
    }
}

/*
 * This function gets called twice during initial import per way. Once from add_way and once from out_way
 */
unsigned int tagtransform::filter_way_tags(struct keyval *tags, int * polygon, int * roads,
                                          const export_list *exlist, bool strict) {
    if (transform_method) {
#ifdef HAVE_LUA
        return lua_filter_basic_tags(OSMTYPE_WAY, tags, polygon, roads);
#else
        return 1;
#endif
    } else {
        return c_filter_basic_tags(OSMTYPE_WAY, tags, polygon, roads, exlist, strict);
    }
}

unsigned int tagtransform::filter_rel_tags(struct keyval *tags, const export_list *exlist, bool strict) {
    int poly, roads;
    if (transform_method) {
        return lua_filter_basic_tags(OSMTYPE_RELATION, tags, &poly, &roads);
    } else {
        return c_filter_basic_tags(OSMTYPE_RELATION, tags, &poly, &roads, exlist, strict);
    }
}

unsigned int tagtransform::filter_rel_member_tags(struct keyval *rel_tags, int member_count, struct keyval *member_tags,const char * const * member_roles, int * member_superseeded, int * make_boundary, int * make_polygon, int * roads, const export_list *exlist, bool allow_typeless) {
    if (transform_method) {
#ifdef HAVE_LUA
        return lua_filter_rel_member_tags(L, m_rel_mem_func.c_str(), rel_tags, member_count, member_tags, member_roles, member_superseeded, make_boundary, make_polygon, roads);
#else
        return 1;
#endif
    } else {
        return c_filter_rel_member_tags(rel_tags, member_count, member_tags, member_roles, member_superseeded, make_boundary, make_polygon, roads, exlist, allow_typeless);
    }
}

unsigned int tagtransform::lua_filter_basic_tags(const OsmType type, keyval *tags, int * polygon, int * roads) {
#ifdef HAVE_LUA
    int filter;
    int count = 0;
    struct keyval *item;
    const char * key, * value;

    *polygon = 0; *roads = 0;

    switch (type) {
    case OSMTYPE_NODE: {
        lua_getglobal(L, m_node_func.c_str());
        break;
    }
    case OSMTYPE_WAY: {
        lua_getglobal(L, m_way_func.c_str());
        break;
    }
    case OSMTYPE_RELATION: {
        lua_getglobal(L, m_rel_func.c_str());
        break;
    }
    }

    lua_newtable(L);    /* key value table */

    while( (item = tags->popItem()) != NULL ) {
        lua_pushstring(L, item->key.c_str());
        lua_pushstring(L, item->value.c_str());
        lua_rawset(L, -3);
        delete(item);
        count++;
    }

    //printf("C count %i\n", count);
    lua_pushinteger(L, count);

    if (lua_pcall(L,2,type == OSMTYPE_WAY ? 4 : 2,0)) {
        fprintf(stderr, "Failed to execute lua function for basic tag processing: %s\n", lua_tostring(L, -1));
        /* lua function failed */
        return 1;
    }

    if (type == OSMTYPE_WAY) {
        *roads = lua_tointeger(L, -1);
        lua_pop(L,1);
        *polygon = lua_tointeger(L, -1);
        lua_pop(L,1);
    }

    lua_pushnil(L);
    while (lua_next(L,-2) != 0) {
        key = lua_tostring(L,-2);
        value = lua_tostring(L,-1);
        tags->addItem(key, value, false);
        lua_pop(L,1);
    }

    filter = lua_tointeger(L, -2);

    lua_pop(L,2);

    return filter;
#else
    return 1;
#endif
}

/* Go through the given tags and determine the union of flags. Also remove
 * any tags from the list that we don't know about */
unsigned int tagtransform::c_filter_basic_tags(
    const OsmType type, keyval *tags, int *polygon, int * roads,
    const export_list *exlist, bool strict) {

    //assume we dont like this set of tags
    int filter = 1;

    int flags = 0;
    int add_area_tag = 0;

    //a place to keep the tags we like as we go
    struct keyval temp;

    enum OsmType export_type;
    if (type == OSMTYPE_RELATION) {
        export_type = OSMTYPE_WAY;
    } else {
        export_type = type;
    }

    /* We used to only go far enough to determine if it's a polygon or not, but now we go through and filter stuff we don't need */
    //pop each tag off and keep it in the temp list if we like it
    struct keyval *item;
    while ((item = tags->popItem()) != NULL ) {
        //if we want to do more than the export list says
        if(!strict) {
            if (type == OSMTYPE_RELATION && "type" == item->key) {
                temp.pushItem(item);
                item = NULL;
                filter = 0;
                continue;
            }
            /* Allow named islands to appear as polygons */
            if ("natural" == item->key && "coastline" == item->value) {
                add_area_tag = 1;
            }

            /* Discard natural=coastline tags (we render these from a shapefile instead) */
            if (!options->keep_coastlines && "natural" == item->key
                    && "coastline" == item->value) {
                delete(item);
                item = NULL;
                continue;
            }
        }

        //go through the actual tags found on the item and keep the ones in the export list
        const std::vector<taginfo> &infos = exlist->get(export_type);
        size_t i = 0;
        for (; i < infos.size(); i++) {
            const taginfo &info = infos[i];
            if (wildMatch(info.name.c_str(), item->key.c_str())) {
                if (info.flags & FLAG_DELETE) {
                    delete(item);
                    item = NULL;
                    break;
                }

                filter = 0;
                flags |= info.flags;

                temp.pushItem(item);
                item = NULL;
                break;
            }
        }

        //if we didnt find any tags that we wanted to export and we aren't strictly adhering to the list
        if (i == infos.size() && !strict) {
            if (options->hstore_mode != HSTORE_NONE) {
                /* with hstore, copy all tags... */
                temp.pushItem(item);
                /* ... but if hstore_match_only is set then don't take this
                 as a reason for keeping the object */
                if (!options->hstore_match_only && "osm_uid" != item->key
                        && "osm_user" != item->key
                        && "osm_timestamp" != item->key
                        && "osm_version" != item->key
                        && "osm_changeset" != item->key)
                    filter = 0;
            } else if (options->hstore_columns.size() > 0) {
                /* does this column match any of the hstore column prefixes? */
                size_t j = 0;
                for(; j < options->hstore_columns.size(); ++j) {
                    size_t pos = item->key.find(options->hstore_columns[j]);
                    if (pos == 0) {
                        temp.pushItem(item);
                        /* ... but if hstore_match_only is set then don't take this
                         as a reason for keeping the object */
                        if (!options->hstore_match_only
                                && "osm_uid" != item->key
                                && "osm_user" != item->key
                                && "osm_timestamp" != item->key
                                && "osm_version" != item->key
                                && "osm_changeset" != item->key)
                            filter = 0;
                        break;
                    }
                }
                /* if not, skip the tag */
                if (j == options->hstore_columns.size()) {
                    delete(item);
                }
            } else {
                delete(item);
            }
            item = NULL;
        }
    }

    /* Move from temp list back to original list */
    while ((item = temp.popItem()) != NULL )
        tags->pushItem(item);

    *polygon = flags & FLAG_POLYGON;

    /* Special case allowing area= to override anything else */
    const std::string *area;
    if ((area = tags->getItem("area"))) {
        if (*area == "yes" || *area == "true" || *area == "1")
            *polygon = 1;
        else if (*area == "no" || *area == "false" || *area == "0")
            *polygon = 0;
    } else {
        /* If we need to force this as a polygon, append an area tag */
        if (add_area_tag) {
            tags->addItem("area", "yes", false);
            *polygon = 1;
        }
    }

    if (!filter && (type == OSMTYPE_WAY)) {
        add_z_order(tags,roads);
    }

    return filter;
}
