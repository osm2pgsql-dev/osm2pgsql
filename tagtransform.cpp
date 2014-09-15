
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
    const char *layer = getItem(tags, "layer");
    const char *highway = getItem(tags, "highway");
    const char *bridge = getItem(tags, "bridge");
    const char *tunnel = getItem(tags, "tunnel");
    const char *railway = getItem(tags, "railway");
    const char *boundary = getItem(tags, "boundary");

    int z_order = 0;
    int l;
    unsigned int i;
    char z[13];

    l = layer ? strtol(layer, NULL, 10) : 0;
    z_order = 10 * l;
    *roads = 0;

    if (highway) {
        for (i = 0; i < nLayers; i++) {
            if (!strcmp(layers[i].highway, highway)) {
                z_order += layers[i].offset;
                *roads = layers[i].roads;
                break;
            }
        }
    }

    if (railway && strlen(railway)) {
        z_order += 5;
        *roads = 1;
    }
    /* Administrative boundaries are rendered at low zooms so we prefer to use the roads table */
    if (boundary && !strcmp(boundary, "administrative"))
        *roads = 1;

    if (bridge
            && (!strcmp(bridge, "true") || !strcmp(bridge, "yes")
                    || !strcmp(bridge, "1")))
        z_order += 10;

    if (tunnel
            && (!strcmp(tunnel, "true") || !strcmp(tunnel, "yes")
                    || !strcmp(tunnel, "1")))
        z_order -= 10;

    snprintf(z, sizeof(z), "%d", z_order);
    addItem(tags, "z_order", z, 0);

    return 0;
}

unsigned int c_filter_rel_member_tags(
        keyval *rel_tags, const int member_count,
        keyval *member_tags, const char * const *member_roles,
        int * member_superseeded, int * make_boundary, int * make_polygon, int * roads,
        const export_list *exlist, bool allow_typeless) {

    char *type;
    struct keyval tags, *p, *q, *qq, poly_tags;
    int i, j;
    int first_outerway, contains_tag;

    //if it has a relation figure out what kind it is
    type = getItem(rel_tags, "type");
    bool is_route = false, is_boundary = false, is_multipolygon = false;
    if(type)
    {
        //what kind of relation is it
        is_route = strcmp(type, "route") == 0;
        is_boundary = strcmp(type, "boundary") == 0;
        is_multipolygon = strcmp(type, "multipolygon") == 0;
    }//you didnt have a type and it was required
    else if(!allow_typeless)
    {
        return 1;
    }

    /* Clone tags from relation */
    initList(&tags);
    initList(&poly_tags);
    p = rel_tags->next;
    while (p != rel_tags) {
        //copy the name tag as "route_name"
        if (is_route && (strcmp(p->key, "name") == 0))
            addItem(&tags, "route_name", p->value, 1);
        //copy all other tags except for "type"
        else if (strcmp(p->key, "type"))
            addItem(&tags, p->key, p->value, 1);
        p = p->next;
    }

    if (is_route) {
        const char *state = getItem(rel_tags, "state");
        const char *netw = getItem(rel_tags, "network");
        int networknr = -1;

        if (state == NULL ) {
            state = "";
        }

        if (netw != NULL ) {
            if (strcmp(netw, "lcn") == 0) {
                networknr = 10;
                if (strcmp(state, "alternate") == 0) {
                    addItem(&tags, "lcn", "alternate", 1);
                } else if (strcmp(state, "connection") == 0) {
                    addItem(&tags, "lcn", "connection", 1);
                } else {
                    addItem(&tags, "lcn", "yes", 1);
                }
            } else if (strcmp(netw, "rcn") == 0) {
                networknr = 11;
                if (strcmp(state, "alternate") == 0) {
                    addItem(&tags, "rcn", "alternate", 1);
                } else if (strcmp(state, "connection") == 0) {
                    addItem(&tags, "rcn", "connection", 1);
                } else {
                    addItem(&tags, "rcn", "yes", 1);
                }
            } else if (strcmp(netw, "ncn") == 0) {
                networknr = 12;
                if (strcmp(state, "alternate") == 0) {
                    addItem(&tags, "ncn", "alternate", 1);
                } else if (strcmp(state, "connection") == 0) {
                    addItem(&tags, "ncn", "connection", 1);
                } else {
                    addItem(&tags, "ncn", "yes", 1);
                }

            } else if (strcmp(netw, "lwn") == 0) {
                networknr = 20;
                if (strcmp(state, "alternate") == 0) {
                    addItem(&tags, "lwn", "alternate", 1);
                } else if (strcmp(state, "connection") == 0) {
                    addItem(&tags, "lwn", "connection", 1);
                } else {
                    addItem(&tags, "lwn", "yes", 1);
                }
            } else if (strcmp(netw, "rwn") == 0) {
                networknr = 21;
                if (strcmp(state, "alternate") == 0) {
                    addItem(&tags, "rwn", "alternate", 1);
                } else if (strcmp(state, "connection") == 0) {
                    addItem(&tags, "rwn", "connection", 1);
                } else {
                    addItem(&tags, "rwn", "yes", 1);
                }
            } else if (strcmp(netw, "nwn") == 0) {
                networknr = 22;
                if (strcmp(state, "alternate") == 0) {
                    addItem(&tags, "nwn", "alternate", 1);
                } else if (strcmp(state, "connection") == 0) {
                    addItem(&tags, "nwn", "connection", 1);
                } else {
                    addItem(&tags, "nwn", "yes", 1);
                }
            }
        }

        if (getItem(rel_tags, "preferred_color") != NULL ) {
            const char *a = getItem(rel_tags, "preferred_color");
            if (strcmp(a, "0") == 0 || strcmp(a, "1") == 0
                    || strcmp(a, "2") == 0 || strcmp(a, "3") == 0
                    || strcmp(a, "4") == 0) {
                addItem(&tags, "route_pref_color", a, 1);
            } else {
                addItem(&tags, "route_pref_color", "0", 1);
            }
        } else {
            addItem(&tags, "route_pref_color", "0", 1);
        }

        if (getItem(rel_tags, "ref") != NULL ) {
            if (networknr == 10) {
                addItem(&tags, "lcn_ref", getItem(rel_tags, "ref"), 1);
            } else if (networknr == 11) {
                addItem(&tags, "rcn_ref", getItem(rel_tags, "ref"), 1);
            } else if (networknr == 12) {
                addItem(&tags, "ncn_ref", getItem(rel_tags, "ref"), 1);
            } else if (networknr == 20) {
                addItem(&tags, "lwn_ref", getItem(rel_tags, "ref"), 1);
            } else if (networknr == 21) {
                addItem(&tags, "rwn_ref", getItem(rel_tags, "ref"), 1);
            } else if (networknr == 22) {
                addItem(&tags, "nwn_ref", getItem(rel_tags, "ref"), 1);
            }
        }
    } else if (is_boundary) {
        /* Boundaries will get converted into multiple geometries:
         - Linear features will end up in the line and roads tables (useful for admin boundaries)
         - Polygon features also go into the polygon table (useful for national_forests)
         The edges of the polygon also get treated as linear fetaures allowing these to be rendered seperately. */
        *make_boundary = 1;
    } else if (is_multipolygon && getItem(&tags, "boundary")) {
        /* Treat type=multipolygon exactly like type=boundary if it has a boundary tag. */
        *make_boundary = 1;
    } else if (is_multipolygon) {
        *make_polygon = 1;

        /* Collect a list of polygon-like tags, these are used later to
         identify if an inner rings looks like it should be rendered separately */
        p = tags.next;
        while (p != &tags) {
            if (!strcmp(p->key, "area")) {
                addItem(&poly_tags, p->key, p->value, 1);
            } else {
                const std::vector<taginfo> &infos = exlist->get(OSMTYPE_WAY);
                for (i = 0; i < infos.size(); i++) {
                    const taginfo &info = infos[i];
                    if (strcmp(info.name.c_str(), p->key) == 0) {
                        if (info.flags & FLAG_POLYGON) {
                            addItem(&poly_tags, p->key, p->value, 1);
                        }
                        break;
                    }
                }
            }
            p = p->next;
        }

        /* Copy the tags from the outer way(s) if the relation is untagged (with
         * respect to tags that influence its polygon nature. Tags like name or fixme should be fine*/
        if (!listHasData(&poly_tags)) {
            first_outerway = 1;
            for (i = 0; i < member_count; i++) {
                if (member_roles[i] && !strcmp(member_roles[i], "inner"))
                    continue;

                /* insert all tags of the first outerway to the potential list of copied tags. */
                if (first_outerway) {
                    p = member_tags[i].next;
                    while (p != &(member_tags[i])) {
                        addItem(&poly_tags, p->key, p->value, 1);                        
                        p = p->next;
                    }
                } else {
                    /* Check if all of the tags in the list of potential tags are present on this way,
                       otherwise remove from the list of potential tags. Tags need to be present on
                       all outer ways to be copied over to the relation */
                    q = poly_tags.next; 
                    while (q != &poly_tags) {
                        p = getTag(&(member_tags[i]), q->key);
                        if ((p != NULL) && (strcmp(q->value, p->value) == 0)) {
                            q = q->next;
                        } else {
                            /* This tag is not present on all member outer ways, so don't copy it over to relation */
                            qq = q->next;
                            removeTag(q);
                            q = qq;
                        }
                    }
                }
                first_outerway = 0;
            }
            /* Copy the list identified outer way tags over to the relation */
            q = poly_tags.next; 
            while (q != &poly_tags) {
                addItem(&tags, q->key, q->value, 1);                        
                q = q->next;
            }

            /* We need to re-check and only keep polygon tags in the list of polytags */
            q = poly_tags.next; 
            while (q != &poly_tags) {
                contains_tag = 0;
                const std::vector<taginfo> &infos = exlist->get(OSMTYPE_WAY);
                for (j = 0; j < infos.size(); j++) {
                    const taginfo &info = infos[j];
                    if (strcmp(info.name.c_str(), q->key) == 0) {
                        if (info.flags & FLAG_POLYGON) {
                            contains_tag = 1;
                            break;
                        }
                    }
                }
                if (contains_tag == 0) {
                    qq = q->next;
                    removeTag(q);
                    q = qq;
                } else {
                    q = q->next;
                }
            }
        }
        resetList(&poly_tags);
    } else if(!allow_typeless) {
        /* Unknown type, just exit */
        resetList(&tags);
        resetList(&poly_tags);
        return 1;
    }

    if (!listHasData(&tags)) {
        resetList(&tags);
        resetList(&poly_tags);
        return 1;
    }

    /* If we are creating a multipolygon then we
     mark each member so that we can skip them during iterate_ways
     but only if the polygon-tags look the same as the outer ring */
    if (make_polygon) {
        for (i = 0; i < member_count; i++) {
            int match = 1;
            struct keyval *p = member_tags[i].next;
            while (p != &(member_tags[i])) {
                const char *v = getItem(&tags, p->key);
                if (!v || strcmp(v, p->value)) {
                    /* z_order and osm_ are automatically generated tags, so ignore them */
                    if ((strcmp(p->key, "z_order") != 0) && (strcmp(p->key, "osm_user") != 0) && 
                        (strcmp(p->key, "osm_version") != 0) && (strcmp(p->key, "osm_uid") != 0) &&
                        (strcmp(p->key, "osm_changeset")) && (strcmp(p->key, "osm_timestamp") != 0)) {
                        match = 0;
                        break;
                    }
                }
                p = p->next;
            }
            if (match) {
                member_superseeded[i] = 1;
            } else {
                member_superseeded[i] = 0;
            }
        }
    }

    resetList(rel_tags);
    cloneList(rel_tags, &tags);
    resetList(&tags);

    add_z_order(rel_tags, roads);

    return 0;
}

#ifdef HAVE_LUA
unsigned int lua_filter_rel_member_tags(lua_State* L, const char* rel_mem_func, keyval *rel_tags, const int member_count,
        keyval *member_tags,const char * const * member_roles,
        int * member_superseeded, int * make_boundary, int * make_polygon, int * roads) {

    int i;
    int idx = 0;
    int filter;
    int count = 0;
    struct keyval *item;
    const char * key, * value;

    lua_getglobal(L, rel_mem_func);

    lua_newtable(L);    /* relations key value table */

    idx = 1;
    while( (item = popItem(rel_tags)) != NULL ) {
        lua_pushstring(L, item->key);
        lua_pushstring(L, item->value);
        lua_rawset(L, -3);
        freeItem(item);
        count++;
    }

    lua_newtable(L);    /* member tags table */

    for (i = 1; i <= member_count; i++) {
        lua_pushnumber(L, i);
        lua_newtable(L);    /* member key value table */
        while( (item = popItem(&(member_tags[i - 1]))) != NULL ) {
            lua_pushstring(L, item->key);
            lua_pushstring(L, item->value);
            lua_rawset(L, -3);
            freeItem(item);
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
        addItem(rel_tags, key, value, 0);
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
    int idx = 0;
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

    idx = 1;
    while( (item = popItem(tags)) != NULL ) {
        lua_pushstring(L, item->key);
        lua_pushstring(L, item->value);
        lua_rawset(L, -3);
        freeItem(item);
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
        addItem(tags, key, value, 0);
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
    initList(&temp);

    enum OsmType export_type;
    if (type == OSMTYPE_RELATION) {
        export_type = OSMTYPE_WAY;
    } else {
        export_type = type;
    }

    /* We used to only go far enough to determine if it's a polygon or not, but now we go through and filter stuff we don't need */
    //pop each tag off and keep it in the temp list if we like it
    struct keyval *item;
    while ((item = popItem(tags)) != NULL ) {
        //if we want to do more than the export list says
        if(!strict) {
            if (type == OSMTYPE_RELATION && !strcmp("type", item->key)) {
                pushItem(&temp, item);
                item = NULL;
                filter = 0;
                continue;
            }
            /* Allow named islands to appear as polygons */
            if (!strcmp("natural", item->key)
                    && !strcmp("coastline", item->value)) {
                add_area_tag = 1;
            }

            /* Discard natural=coastline tags (we render these from a shapefile instead) */
            if (!options->keep_coastlines && !strcmp("natural", item->key)
                    && !strcmp("coastline", item->value)) {
                freeItem(item);
                item = NULL;
                continue;
            }
        }

        //go through the actual tags found on the item and keep the ones in the export list
        const std::vector<taginfo> &infos = exlist->get(export_type);
        size_t i = 0;
        for (; i < infos.size(); i++) {
            const taginfo &info = infos[i];
            if (wildMatch(info.name.c_str(), item->key)) {
                if (info.flags & FLAG_DELETE) {
                    freeItem(item);
                    item = NULL;
                    break;
                }

                filter = 0;
                flags |= info.flags;

                pushItem(&temp, item);
                item = NULL;
                break;
            }
        }

        //if we didnt find any tags that we wanted to export and we aren't strictly adhering to the list
        if (i == infos.size() && !strict) {
            if (options->hstore_mode != HSTORE_NONE) {
                /* with hstore, copy all tags... */
                pushItem(&temp, item);
                /* ... but if hstore_match_only is set then don't take this
                 as a reason for keeping the object */
                if (!options->hstore_match_only && strcmp("osm_uid", item->key)
                        && strcmp("osm_user", item->key)
                        && strcmp("osm_timestamp", item->key)
                        && strcmp("osm_version", item->key)
                        && strcmp("osm_changeset", item->key))
                    filter = 0;
            } else if (options->hstore_columns.size() > 0) {
                /* does this column match any of the hstore column prefixes? */
                size_t j = 0;
                for(; j < options->hstore_columns.size(); ++j) {
                    char *pos = strstr(item->key, options->hstore_columns[j].c_str());
                    if (pos == item->key) {
                        pushItem(&temp, item);
                        /* ... but if hstore_match_only is set then don't take this
                         as a reason for keeping the object */
                        if (!options->hstore_match_only
                                && strcmp("osm_uid", item->key)
                                && strcmp("osm_user", item->key)
                                && strcmp("osm_timestamp", item->key)
                                && strcmp("osm_version", item->key)
                                && strcmp("osm_changeset", item->key))
                            filter = 0;
                        break;
                    }
                }
                /* if not, skip the tag */
                if (j == options->hstore_columns.size()) {
                    freeItem(item);
                }
            } else {
                freeItem(item);
            }
            item = NULL;
        }
    }

    /* Move from temp list back to original list */
    while ((item = popItem(&temp)) != NULL )
        pushItem(tags, item);

    *polygon = flags & FLAG_POLYGON;

    /* Special case allowing area= to override anything else */
    const char *area;
    if ((area = getItem(tags, "area"))) {
        if (!strcmp(area, "yes") || !strcmp(area, "true") || !strcmp(area, "1"))
            *polygon = 1;
        else if (!strcmp(area, "no") || !strcmp(area, "false")
                || !strcmp(area, "0"))
            *polygon = 0;
    } else {
        /* If we need to force this as a polygon, append an area tag */
        if (add_area_tag) {
            addItem(tags, "area", "yes", 0);
            *polygon = 1;
        }
    }

    if (!filter && (type == OSMTYPE_WAY)) {
        add_z_order(tags,roads);
    }

    return filter;
}
