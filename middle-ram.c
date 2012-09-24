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
#include "node-ram-cache.h"

#include "output-pgsql.h"

/* Store +-20,000km Mercator co-ordinates as fixed point 32bit number with maximum precision */
/* Scale is chosen such that 40,000 * SCALE < 2^32          */
#define FIXED_POINT

static int scale = 100;

struct ramWay {
    struct keyval *tags;
    osmid_t *ndids;
    int pending;
};

struct ramRel {
    struct keyval *tags;
    struct member *members;
    int member_count;
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

#define BLOCK_SHIFT 10
#define PER_BLOCK  (1 << BLOCK_SHIFT)
#define NUM_BLOCKS (1 << (32 - BLOCK_SHIFT))

static struct ramWay     *ways[NUM_BLOCKS];
static struct ramRel     *rels[NUM_BLOCKS];

static int node_blocks;
static int way_blocks;

static int way_out_count;
static int rel_out_count;

static inline osmid_t id2block(osmid_t id)
{
    // + NUM_BLOCKS/2 allows for negative IDs
    return (id >> BLOCK_SHIFT) + NUM_BLOCKS/2;
}

static inline osmid_t id2offset(osmid_t id)
{
    return id & (PER_BLOCK-1);
}

static inline int block2id(int block, int offset)
{
    return ((block - NUM_BLOCKS/2) << BLOCK_SHIFT) + offset;
}

#define UNUSED  __attribute__ ((unused))

static int ram_ways_set(osmid_t id, osmid_t *nds, int nd_count, struct keyval *tags, int pending)
{
    int block  = id2block(id);
    int offset = id2offset(id);
    struct keyval *p;

    if (!ways[block]) {
        ways[block] = calloc(PER_BLOCK, sizeof(struct ramWay));
        if (!ways[block]) {
            fprintf(stderr, "Error allocating ways\n");
            exit_nicely();
        }
        way_blocks++;
        //fprintf(stderr, "\tways(%zuMb)\n", way_blocks * sizeof(struct ramWay) * PER_BLOCK / 1000000);
    }

    if (ways[block][offset].ndids) {
        free(ways[block][offset].ndids);
        ways[block][offset].ndids = NULL;
    }

    /* Copy into length prefixed array */
    ways[block][offset].ndids = malloc( (nd_count+1)*sizeof(osmid_t) );
    memcpy( ways[block][offset].ndids+1, nds, nd_count*sizeof(osmid_t) );
    ways[block][offset].ndids[0] = nd_count;
    ways[block][offset].pending = pending;

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

    cloneList(ways[block][offset].tags, tags);

    return 0;
}

static int ram_relations_set(osmid_t id, struct member *members, int member_count, struct keyval *tags)
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

    cloneList(rels[block][offset].tags, tags);

    if (!rels[block][offset].members)
      free( rels[block][offset].members );

    struct member *ptr = malloc(sizeof(struct member) * member_count);
    if (ptr) {
        memcpy( ptr, members, sizeof(struct member) * member_count );
        rels[block][offset].member_count = member_count;
        rels[block][offset].members = ptr;
    } else {
        fprintf(stderr, "%s malloc failed\n", __FUNCTION__);
        exit_nicely();
    }

    return 0;
}

static int ram_nodes_get_list(struct osmNode *nodes, osmid_t *ndids, int nd_count)
{
    int i, count;

    count = 0;
    for( i=0; i<nd_count; i++ )
    {
        if (ram_cache_nodes_get(&nodes[count], ndids[i]))
            continue;

        count++;
    }
    return count;
}

static void ram_iterate_relations(int (*callback)(osmid_t id, struct member *members, int member_count, struct keyval *tags, int))
{
    int block, offset;

    fprintf(stderr, "\n");
    for(block=NUM_BLOCKS-1; block>=0; block--) {
        if (!rels[block])
            continue;

        for (offset=0; offset < PER_BLOCK; offset++) {
            if (rels[block][offset].members) {
                osmid_t id = block2id(block, offset);
                rel_out_count++;
                if (rel_out_count % 10 == 0)
                    fprintf(stderr, "\rWriting relation (%u)", rel_out_count);

                callback(id, rels[block][offset].members, rels[block][offset].member_count, rels[block][offset].tags, 0);
            }
            free(rels[block][offset].members);
            rels[block][offset].members = NULL;
            resetList(rels[block][offset].tags);
            free(rels[block][offset].tags);
            rels[block][offset].tags=NULL;
        }
        free(rels[block]);
        rels[block] = NULL;
    }

    fprintf(stderr, "\rWriting relation (%u)\n", rel_out_count);
}

static void ram_iterate_ways(int (*callback)(osmid_t id, struct keyval *tags, struct osmNode *nodes, int count, int exists))
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
                    fprintf(stderr, "\rWriting way (%uk)", way_out_count/1000);

                if (ways[block][offset].pending) {
                    /* First element contains number of nodes */
                    nodes = malloc( sizeof(struct osmNode) * ways[block][offset].ndids[0]);
                    ndCount = ram_nodes_get_list(nodes, ways[block][offset].ndids+1, ways[block][offset].ndids[0]);

                    if (nodes) {
                        osmid_t id = block2id(block, offset);
                        callback(id, ways[block][offset].tags, nodes, ndCount, 0);
                        free(nodes);
                    }

                    ways[block][offset].pending = 0;
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
    fprintf(stderr, "\rWriting way (%uk)\n", way_out_count/1000);
}

/* Caller must free nodes_ptr and resetList(tags_ptr) */
static int ram_ways_get(osmid_t id, struct keyval *tags_ptr, struct osmNode **nodes_ptr, int *count_ptr)
{
    int block = id2block(id), offset = id2offset(id), ndCount = 0;
    struct osmNode *nodes;

    if (!ways[block])
        return 1;

    if (ways[block][offset].ndids) {
        /* First element contains number of nodes */
        nodes = malloc( sizeof(struct osmNode) * ways[block][offset].ndids[0]);
        ndCount = ram_nodes_get_list(nodes, ways[block][offset].ndids+1, ways[block][offset].ndids[0]);

        if (ndCount) {
            cloneList( tags_ptr, ways[block][offset].tags );
            *nodes_ptr = nodes;
            *count_ptr = ndCount;
            return 0;
        }
    }
    return 1;
}

static int ram_ways_get_list(osmid_t *ids, int way_count, osmid_t **way_ids, struct keyval *tag_ptr, struct osmNode **node_ptr, int *count_ptr) {
    int count = 0;
    *way_ids = malloc( sizeof(osmid_t) * (way_count + 1));
    initList(&(tag_ptr[count]));
    for (int i = 0; i < way_count; i++) {
        
        if (ram_ways_get(ids[i], &(tag_ptr[count]), &(node_ptr[count]), &(count_ptr[count])) == 0) {
            (*way_ids)[count] = ids[i];
            count++;
            initList(&(tag_ptr[count]));
        }
    }
    return count;
}

// Marks the way so that iterate ways skips it
static int ram_ways_done(osmid_t id)
{
    int block = id2block(id), offset = id2offset(id);

    if (!ways[block])
        return 1;

    ways[block][offset].pending = 0;
    return 0;
}

static void ram_analyze(void)
{
    /* No need */
}

static void ram_end(void)
{
    /* No need */
}

static int ram_start(const struct output_options *options)
{
    // latlong has a range of +-180, mercator +-20000
    // The fixed poing scaling needs adjusting accordingly to
    // be stored accurately in an int
    scale = options->scale;

    init_node_ram_cache( options->alloc_chunkwise, options->cache, scale);
    
    fprintf( stderr, "Mid: Ram, scale=%d\n", scale );

    return 0;
}

static void ram_stop(void)
{
    int i, j;
    free_node_ram_cache();

    for (i=0; i<NUM_BLOCKS; i++) {
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

static void ram_commit(void) {
}

struct middle_t mid_ram = {
    .start             = ram_start,
    .stop              = ram_stop,
    .end               = ram_end,
    .cleanup           = ram_stop,
    .analyze           = ram_analyze,
    .commit            = ram_commit,
    .nodes_set         = ram_cache_nodes_set,
#if 0
    .nodes_get         = ram_nodes_get,
#endif
    .nodes_get_list    = ram_nodes_get_list,
    .ways_set          = ram_ways_set,
    .ways_get          = ram_ways_get,
    .ways_get_list     = ram_ways_get_list,
    .ways_done         = ram_ways_done,

    .relations_set     = ram_relations_set,
#if 0
    .iterate_nodes     = ram_iterate_nodes,
#endif
    .iterate_ways      = ram_iterate_ways,
    .iterate_relations = ram_iterate_relations
};

