
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "osmtypes.h"
#include "keyvals.h"
#include "tagtransform.h"
#include "output-pgsql.h"
#include "config.h"
#include "wildcmp.h"

#ifdef HAVE_LUA
static lua_State *L;
#endif

static struct {
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

const struct output_options *options;

extern struct taginfo *exportList[4]; /* Indexed by enum table_id */
extern int exportListCount[4];

int transform_method = 0;

static int add_z_order(struct keyval *tags, int *roads) {
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

static unsigned int tagtransform_lua_filter_basic_tags(enum OsmType type, struct keyval *tags, int * polygon, int * roads) {
#ifdef HAVE_LUA
    int idx = 0;
    int filter;
    int count = 0;
    struct keyval *item;
    const char * key, * value;

    *polygon = 0; *roads = 0;

    switch (type) {
    case OSMTYPE_NODE: {
        lua_getglobal(L, "filter_tags_node");
        break;
    }
    case OSMTYPE_WAY: {
        lua_getglobal(L, "filter_tags_way");
        break;
    }
    case OSMTYPE_RELATION: {
        lua_getglobal(L, "filter_basic_tags_rel");
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
static unsigned int tagtransform_c_filter_basic_tags(enum OsmType type,
        struct keyval *tags, int *polygon, int * roads) {
    int i, filter = 1;
    int flags = 0;
    int add_area_tag = 0;
    enum OsmType export_type;

    const char *area;
    struct keyval *item;
    struct keyval temp;
    initList(&temp);

    if (type == OSMTYPE_RELATION) {export_type = OSMTYPE_WAY;} else {export_type = type;}

    /* We used to only go far enough to determine if it's a polygon or not, but now we go through and filter stuff we don't need */
    while ((item = popItem(tags)) != NULL ) {
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

        for (i = 0; i < exportListCount[export_type]; i++) {
            if (wildMatch(exportList[export_type][i].name, item->key)) {
                if (exportList[export_type][i].flags & FLAG_DELETE) {
                    freeItem(item);
                    item = NULL;
                    break;
                }

                filter = 0;
                flags |= exportList[export_type][i].flags;

                pushItem(&temp, item);
                item = NULL;
                break;
            }
        }

        /** if tag not found in list of exports: */
        if (i == exportListCount[export_type]) {
            if (options->enable_hstore) {
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
            } else if (options->n_hstore_columns) {
                /* does this column match any of the hstore column prefixes? */
                int j;
                for (j = 0; j < options->n_hstore_columns; j++) {
                    char *pos = strstr(item->key, options->hstore_columns[j]);
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
                if (j == options->n_hstore_columns) {
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


static unsigned int tagtransform_lua_filter_rel_member_tags(struct keyval *rel_tags, int member_count,
        struct keyval *member_tags,const char **member_role,
        int * member_superseeded, int * make_boundary, int * make_polygon, int * roads) {
#ifdef HAVE_LUA

    int i;
    int idx = 0;
    int filter;
    int count = 0;
    struct keyval *item;
    const char * key, * value;

    lua_getglobal(L, "filter_tags_relation_member");

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
        lua_pushstring(L, member_role[i]);
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
#else
    return 1;
#endif
}


static unsigned int tagtransform_c_filter_rel_member_tags(
        struct keyval *rel_tags, int member_count,
        struct keyval *member_tags, const char **member_role,
        int * member_superseeded, int * make_boundary, int * make_polygon, int * roads) {
    char *type;
    struct keyval tags, *p, *q, *qq, poly_tags;
    int i, j;
    int first_outerway, contains_tag;

    /* Get the type, if there's no type we don't care */
    type = getItem(rel_tags, "type");
    if (!type)
        return 1;

    initList(&tags);
    initList(&poly_tags);

    /* Clone tags from relation */
    p = rel_tags->next;
    while (p != rel_tags) {
        /* For routes, we convert name to route_name */
        if ((strcmp(type, "route") == 0) && (strcmp(p->key, "name") == 0))
            addItem(&tags, "route_name", p->value, 1);
        else if (strcmp(p->key, "type")) /* drop type= */
            addItem(&tags, p->key, p->value, 1);
        p = p->next;
    }

    if (strcmp(type, "route") == 0) {
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
    } else if (strcmp(type, "boundary") == 0) {
        /* Boundaries will get converted into multiple geometries:
         - Linear features will end up in the line and roads tables (useful for admin boundaries)
         - Polygon features also go into the polygon table (useful for national_forests)
         The edges of the polygon also get treated as linear fetaures allowing these to be rendered seperately. */
        *make_boundary = 1;
    } else if (strcmp(type, "multipolygon") == 0
            && getItem(&tags, "boundary")) {
        /* Treat type=multipolygon exactly like type=boundary if it has a boundary tag. */
        *make_boundary = 1;
    } else if (strcmp(type, "multipolygon") == 0) {
        *make_polygon = 1;

        /* Collect a list of polygon-like tags, these are used later to
         identify if an inner rings looks like it should be rendered separately */
        p = tags.next;
        while (p != &tags) {
            if (!strcmp(p->key, "area")) {
                addItem(&poly_tags, p->key, p->value, 1);
            } else {
                for (i = 0; i < exportListCount[OSMTYPE_WAY]; i++) {
                    if (strcmp(exportList[OSMTYPE_WAY][i].name, p->key) == 0) {
                        if (exportList[OSMTYPE_WAY][i].flags & FLAG_POLYGON) {
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
                if (member_role[i] && !strcmp(member_role[i], "inner"))
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
                for (j = 0; j < exportListCount[OSMTYPE_WAY]; j++) {
                    if (strcmp(exportList[OSMTYPE_WAY][j].name, q->key) == 0) {
                        if (exportList[OSMTYPE_WAY][j].flags & FLAG_POLYGON) {
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
    } else {
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
                        (strcmp(p->key, "osm_changeset"))) {
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

static int tagtransform_lua_init() {
#ifdef HAVE_LUA
    L = luaL_newstate();
    luaL_openlibs(L);
    luaL_dofile(L, options->tag_transform_script);

    lua_getglobal(L, "filter_tags_node");
    if (!lua_isfunction (L, -1)) {
        fprintf(stderr,"Tag transform style does not contain a function filter_tags_node\n");
        return 1;
    }
    lua_pop(L,1);

    lua_getglobal(L, "filter_tags_way");
    if (!lua_isfunction (L, -1)) {
        fprintf(stderr,"Tag transform style does not contain a function filter_tags_way\n");
        return 1;
    }
    lua_pop(L,1);

    lua_getglobal(L, "filter_basic_tags_rel");
    if (!lua_isfunction (L, -1)) {
        fprintf(stderr,"Tag transform style does not contain a function filter_basic_tags_rel\n");
        return 1;
    }

    lua_getglobal(L, "filter_tags_relation_member");
    if (!lua_isfunction (L, -1)) {
        fprintf(stderr,"Tag transform style does not contain a function filter_tags_relation_member\n");
        return 1;
    }

    return 0;
#else
    fprintf(stderr,"Error: Could not init lua tag transform, as lua support was not compiled into this version\n");
    return 1;
#endif
}

void tagtransform_lua_shutdown() {
#ifdef HAVE_LUA
    lua_close(L);
#endif
}

unsigned int tagtransform_filter_node_tags(struct keyval *tags) {
    int poly, roads;
    if (transform_method) {
        return tagtransform_lua_filter_basic_tags(OSMTYPE_NODE, tags, &poly, &roads);
    } else {
        return tagtransform_c_filter_basic_tags(OSMTYPE_NODE, tags, &poly, &roads);
    }
}

/*
 * This function gets called twice during initial import per way. Once from add_way and once from out_way
 */
unsigned int tagtransform_filter_way_tags(struct keyval *tags, int * polygon, int * roads) {
    if (transform_method) {
        return tagtransform_lua_filter_basic_tags(OSMTYPE_WAY, tags, polygon, roads);
    } else {
        return tagtransform_c_filter_basic_tags(OSMTYPE_WAY, tags, polygon, roads);
    }
}

unsigned int tagtransform_filter_rel_tags(struct keyval *tags) {
    int poly, roads;
    if (transform_method) {
        return tagtransform_lua_filter_basic_tags(OSMTYPE_RELATION, tags, &poly, &roads);
    } else {
        return tagtransform_c_filter_basic_tags(OSMTYPE_RELATION, tags, &poly, &roads);
    }
}

unsigned int tagtransform_filter_rel_member_tags(struct keyval *rel_tags, int member_count, struct keyval *member_tags,const char **member_role, int * member_superseeded, int * make_boundary, int * make_polygon, int * roads) {
    if (transform_method) {
        return tagtransform_lua_filter_rel_member_tags(rel_tags, member_count, member_tags, member_role, member_superseeded, make_boundary, make_polygon, roads);
    } else {
        return tagtransform_c_filter_rel_member_tags(rel_tags, member_count, member_tags, member_role, member_superseeded, make_boundary, make_polygon, roads);
    }
}

int tagtransform_init(const struct output_options *opts) {
    options = opts;
    if (opts->tag_transform_script) {
        transform_method = 1;
        fprintf(stderr, "Using lua based tag processing pipeline with script %s\n", opts->tag_transform_script);
        return tagtransform_lua_init();
    } else  {
        transform_method = 0;
        fprintf(stderr, "Using built-in tag processing pipeline\n");
        return 0; //Nothing to initialise
    }
}

void tagtransform_shutdown() {
    if (transform_method)
        tagtransform_lua_shutdown();
}
