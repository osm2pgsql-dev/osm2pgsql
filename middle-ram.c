/* Implements the mid-layer processing for osm2pgsql
 * using several arrays in RAM. This is fastest if you
 * have sufficient RAM+Swap.
 *
 * This layer stores data read in from the planet.osm file
 * and is then read by the backend processing code to
 * emit the final geometry-enabled output formats
*/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <libpq-fe.h>

#include "osmtypes.h"
#include "middle.h"
#include "middle-ram.h"

#include "output-pgsql.h"

/* Store +-20,000km Mercator co-ordinates as fixed point 32bit number with maximum precision */
/* Scale is chosen such that 40,000 * SCALE < 2^32          */
#define FIXED_POINT

// Need clean mode so that we can handle polygons with holes
#define USE_CLEAN

static int scale = 100;
#define DOUBLE_TO_FIX(x) ((x) * scale)
#define FIX_TO_DOUBLE(x) (((double)x) / scale)

struct ramNode {
#ifdef FIXED_POINT
    int lon;
    int lat;
#else
    double lon;
    double lat;
#endif
};

struct ramWay {
    struct keyval *tags;
    int *ndids;
};

struct ramRel {
    struct keyval *tags;
    struct keyval *members;
};

/* Object storage now uses 2 levels of storage arrays.
 *
 * - Low level storage of 2^16 (~65k) objects in an indexed array
 *   These are allocated dynamically when we need to first store data with
 *   an ID in this block
 *
 * - Fixed array of 2^(32 - 16) = 65k pointers to the dynamically allocated arrays.
 *
 * This allows memory usage to be efficient and scale dynamically without needing to
 * hard code maximum IDs. We now support an ID  range of -2^31 to +2^31.
 * The negative IDs often occur in non-uploaded JOSM data or other data import scripts.
 *
 */

#define BLOCK_SHIFT 16
#define PER_BLOCK  (1 << BLOCK_SHIFT)
#define NUM_BLOCKS (1 << (32 - BLOCK_SHIFT))

static struct ramNode    *nodes[NUM_BLOCKS];
static struct ramWay     *ways[NUM_BLOCKS];
static struct ramRel     *rels[NUM_BLOCKS];

static int node_blocks;
#ifdef USE_CLEAN
static int way_blocks;
#endif

static int way_out_count, rel_out_count;

static struct osmNode *getNodes(int *ndids, int *ndCount);

static inline int id2block(int id)
{
    // + NUM_BLOCKS/2 allows for negative IDs
    return (id >> BLOCK_SHIFT) + NUM_BLOCKS/2;
}

static inline int id2offset(int id)
{
    return id & (PER_BLOCK-1);
}

static inline int block2id(int block, int offset)
{
    return ((block - NUM_BLOCKS/2) << BLOCK_SHIFT) + offset;
}

static int ram_nodes_set(int id, double lat, double lon, struct keyval *tags)
{
    int block  = id2block(id);
    int offset = id2offset(id);

    if (!nodes[block]) {
        nodes[block] = calloc(PER_BLOCK, sizeof(struct ramNode));
        if (!nodes[block]) {
            fprintf(stderr, "Error allocating nodes\n");
            exit_nicely();
        }
        node_blocks++;
        //fprintf(stderr, "\tnodes(%zuMb)\n", node_blocks * sizeof(struct ramNode) * PER_BLOCK / 1000000);
    }

#ifdef FIXED_POINT
    nodes[block][offset].lat = DOUBLE_TO_FIX(lat);
    nodes[block][offset].lon = DOUBLE_TO_FIX(lon);
#else
    nodes[block][offset].lat = lat;
    nodes[block][offset].lon = lon;
#endif

#if 0
    while ((p = popItem(tags)) != NULL)
    pushItem(&nodes[block][offset].tags, p);
#else
    /* FIXME: This is a performance hack which interferes with a clean middle / output separation */
    out_pgsql.node(id, tags, lat, lon);
    resetList(tags);
#endif
    return 0;
}


static int ram_nodes_get(struct osmNode *out, int id)
{
    int block  = id2block(id);
    int offset = id2offset(id);

    if (!nodes[block])
        return 1;

    if (!nodes[block][offset].lat && !nodes[block][offset].lon)
        return 1;

#ifdef FIXED_POINT
    out->lat = FIX_TO_DOUBLE(nodes[block][offset].lat);
    out->lon = FIX_TO_DOUBLE(nodes[block][offset].lon);
#else
    out->lat = nodes[block][offset].lat;
    out->lon = nodes[block][offset].lon;
#endif
    return 0;
}

static int ram_ways_set(int id, struct keyval *nds, struct keyval *tags)
{
    struct keyval *p;
    int *ndids;
    int ndCount, i;
#ifdef USE_CLEAN
    int block  = id2block(id);
    int offset = id2offset(id);
#else
    struct osmNode *nodes;
#endif

#ifdef USE_CLEAN
    if (!ways[block]) {
        ways[block] = calloc(PER_BLOCK, sizeof(struct ramWay));
        if (!ways[block]) {
            fprintf(stderr, "Error allocating ways\n");
            exit_nicely();
        }
        way_blocks++;
        //fprintf(stderr, "\tways(%zuMb)\n", way_blocks * sizeof(struct ramWay) * PER_BLOCK / 1000000);
    }

    ndCount = countList(nds);
    if (!ndCount)
        return 1;

    if (ways[block][offset].ndids) {
        free(ways[block][offset].ndids);
        ways[block][offset].ndids = NULL;
    }
#endif

    ndCount = countList(nds);
    if (!ndCount)
        return 1;

    ndids = malloc(sizeof(int) * (ndCount+1));
    if (!ndids) {
        fprintf(stderr, "%s malloc failed\n", __FUNCTION__);
        exit_nicely();
    }

#ifdef USE_CLEAN
    ways[block][offset].ndids = ndids;
#endif
    i = 0;
    while ((p = popItem(nds)) != NULL) {
        int nd_id = strtol(p->value, NULL, 10);
        freeItem(p);
        if (nd_id) /* id = 0 is used as list terminator */
            ndids[i++] = nd_id;
    }
    ndids[i] = 0;
#ifdef USE_CLEAN
    if (!ways[block][offset].tags) {
        p = malloc(sizeof(struct keyval));
        if (p) {
            initList(p);
            ways[block][offset].tags = p;
        } else {
            fprintf(stderr, "%s malloc failed\n", __FUNCTION__);
            exit_nicely();
        }
    } else
        resetList(ways[block][offset].tags);

        while ((p = popItem(tags)) != NULL)
            pushItem(ways[block][offset].tags, p);
#else
    nodes = getNodes(ndids, &ndCount);
    free(ndids);

    if (nodes) {
        out_pgsql.way(id, tags, nodes, ndCount);
        free(nodes);
    }
#endif
    return 0;
}

static int ram_relations_set(int id, struct keyval *members, struct keyval *tags)
{
    struct keyval *p;
    int block  = id2block(id);
    int offset = id2offset(id);

    if (!rels[block]) {
        rels[block] = calloc(PER_BLOCK, sizeof(struct ramRel));
        if (!rels[block]) {
            fprintf(stderr, "Error allocating rels\n");
            exit_nicely();
        }
    }

    if (!rels[block][offset].tags) {
        p = malloc(sizeof(struct keyval));
        if (p) {
            initList(p);
            rels[block][offset].tags = p;
        } else {
            fprintf(stderr, "%s malloc failed\n", __FUNCTION__);
            exit_nicely();
        }
    } else
        resetList(rels[block][offset].tags);

    while ((p = popItem(tags)) != NULL)
        pushItem(rels[block][offset].tags, p);

    if (!rels[block][offset].members) {
        p = malloc(sizeof(struct keyval));
        if (p) {
            initList(p);
            rels[block][offset].members = p;
        } else {
            fprintf(stderr, "%s malloc failed\n", __FUNCTION__);
            exit_nicely();
        }
    } else
        resetList(rels[block][offset].members);

    while ((p = popItem(members)) != NULL)
        pushItem(rels[block][offset].members, p);

    return 0;
    //return out_pgsql.relation(id, tags, nodes, ndCount)
}

static struct osmNode *getNodes(int *ndids, int *ndCount)
{
    int id;
    int i, count = 0;
    struct osmNode *nodes;

    while(ndids[count])
        count++;

    nodes = malloc(count * sizeof(struct osmNode));

    if (!nodes) {
        *ndCount = 0;
        return NULL;
    }

    count = 0;
    i = 0;
    while ((id = ndids[i++]) != 0)
    {
        if (ram_nodes_get(&nodes[count], id))
            continue;

        count++;
    }
    *ndCount = count;
    return nodes;
}

static struct osmNode **getRelNodes(struct keyval *members, struct keyval ***pway_tags, int **pndCount)
{
    // Gather all ways referenced by a relation
    struct keyval *m = members->next;
    int count = countList(members)+1;
    struct osmNode **relNodes = calloc(count, sizeof(struct osmNode *));
    struct keyval **way_tags = calloc(count, sizeof(struct keyval *));
    int i = 0;
    int *ndCount;

    assert(pndCount);
    assert(relNodes);
    assert(way_tags);
    assert(pway_tags);
    ndCount = calloc(count, sizeof(int));
    assert(ndCount);
    *pndCount = ndCount;
    *pway_tags = way_tags;

    while (m != members) {
        int way_id = atoi(m->value);
        if (way_id) {
            int block  = id2block(way_id);
            int offset = id2offset(way_id);
            if (ways[block] && ways[block][offset].ndids) {
                relNodes[i] = getNodes(ways[block][offset].ndids, &ndCount[i]);
                way_tags[i] = ways[block][offset].tags;
                if (relNodes[i]) 
                    i++;
            }
        }
        m = m->next;
    }
    relNodes[i] = NULL;
    ndCount[i] = 0;
    way_tags[i] = NULL;
    return relNodes;
}

static void ram_iterate_relations(int (*callback)(int id, struct keyval *rel_tags, struct osmNode **nodes, struct keyval **tags, int *count))
{
    int block, offset, *ndCount = NULL;
    struct osmNode **nodes;
    struct keyval **way_tags;

    fprintf(stderr, "\n");
    for(block=NUM_BLOCKS-1; block>=0; block--) {
        if (!rels[block])
            continue;

        for (offset=0; offset < PER_BLOCK; offset++) {
            if (rels[block][offset].members) {
                rel_out_count++;
                if (rel_out_count % 1000 == 0)
                    fprintf(stderr, "\rWriting rel(%uk)", rel_out_count/1000);

                nodes = getRelNodes(rels[block][offset].members, &way_tags, &ndCount);

                if (nodes) {
                    int i, id = block2id(block, offset);
                    callback(id, rels[block][offset].tags, nodes, way_tags, ndCount);
                    for (i=0; nodes[i]; i++)
                        free(nodes[i]);
                    free(nodes);
                    free(ndCount);
                    free(way_tags);
                    nodes = NULL;
                    ndCount = NULL;
                    way_tags = NULL;
                }
            }
            resetList(rels[block][offset].members);
            free(rels[block][offset].members);
            rels[block][offset].members = NULL;
            resetList(rels[block][offset].tags);
            free(rels[block][offset].tags);
            rels[block][offset].tags=NULL;
        }
        free(rels[block]);
        rels[block] = NULL;
    }

    fprintf(stderr, "\rWriting rel(%uk)\n", rel_out_count/1000);
}


static void ram_iterate_ways(int (*callback)(int id, struct keyval *tags, struct osmNode *nodes, int count))
{
    int block, offset, ndCount = 0;
    struct osmNode *nodes;

    fprintf(stderr, "\n");
    for(block=NUM_BLOCKS-1; block>=0; block--) {
        if (!ways[block])
            continue;

        for (offset=0; offset < PER_BLOCK; offset++) {
            if (ways[block][offset].ndids) {
                way_out_count++;
                if (way_out_count % 1000 == 0)
                    fprintf(stderr, "\rWriting way(%uk)", way_out_count/1000);

                nodes = getNodes(ways[block][offset].ndids, &ndCount);

                if (nodes) {
                    int id = block2id(block, offset);
                    callback(id, ways[block][offset].tags, nodes, ndCount);
                    free(nodes);
                }

                if (ways[block][offset].tags) {
                    resetList(ways[block][offset].tags);
                    free(ways[block][offset].tags);
                    ways[block][offset].tags = NULL;
                }
                if (ways[block][offset].ndids) {
                    free(ways[block][offset].ndids);
                    ways[block][offset].ndids = NULL;
                }
            }
        }
    }
    fprintf(stderr, "\rWriting way(%uk)\n", way_out_count/1000);
}

static void ram_analyze(void)
{
    /* No need */
}

static void ram_end(void)
{
    /* No need */
}

#define __unused  __attribute__ ((unused))
static int ram_start(const char * db __unused, int latlong)
{
    // latlong has a range of +-180, mercator +-20000
    // The fixed poing scaling needs adjusting accordingly to
    // be stored accurately in an int
    scale = latlong ? 10000000 : 100;

    return 0;
}

static void ram_stop(void)
{
    int i, j;

    for (i=0; i<NUM_BLOCKS; i++) {
        if (nodes[i]) {
            free(nodes[i]);
            nodes[i] = NULL;
        }
        if (ways[i]) {
            for (j=0; j<PER_BLOCK; j++) {
                if (ways[i][j].tags) {
                    resetList(ways[i][j].tags);
                    free(ways[i][j].tags);
                }
                if (ways[i][j].ndids)
                    free(ways[i][j].ndids);
            }
            free(ways[i]);
            ways[i] = NULL;
        }
    }
}

struct middle_t mid_ram = {
    start:          ram_start,
    stop:           ram_stop,
    end:            ram_end,
    cleanup:        ram_stop,
    analyze:        ram_analyze,
    nodes_set:      ram_nodes_set,
    nodes_get:      ram_nodes_get,
    ways_set:       ram_ways_set,
    relations_set:  ram_relations_set,
//        ways_get:       ram_ways_get,
//        iterate_nodes:  ram_iterate_nodes,
    iterate_ways:   ram_iterate_ways,
    iterate_relations: ram_iterate_relations
};
