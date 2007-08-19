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
//#define SCALE 10000000
#define SCALE 100
#define DOUBLE_TO_FIX(x) ((x) * SCALE)
#define FIX_TO_DOUBLE(x) (((double)x) / SCALE)

struct ramNode {
#ifdef FIXED_POINT
    int lon;
    int lat;
#else
    double lon;
    double lat;
#endif
};

struct ramSegment {
    int from;
    int to;
};

struct ramWay {
    struct keyval *tags;
    int *segids;
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
static struct ramSegment *segments[NUM_BLOCKS];
static struct ramWay     *ways[NUM_BLOCKS];

static int node_blocks, segment_blocks, way_blocks;

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

static int ram_segments_set(int id, int from, int to, struct keyval *tags)
{
    int block  = id2block(id);
    int offset = id2offset(id);

    if (!segments[block]) {
        segments[block] = calloc(PER_BLOCK, sizeof(struct ramSegment));
        if (!segments[block]) {
            fprintf(stderr, "Error allocating segments\n");
            exit_nicely();
        }
        segment_blocks++;
        //fprintf(stderr, "\tsegments(%zuMb)\n", segment_blocks * sizeof(struct ramSegment) * PER_BLOCK / 1000000);
    }

    segments[block][offset].from = from;
    segments[block][offset].to   = to;
    resetList(tags);
    return 0;
}

static int ram_segments_get(struct osmSegment *out, int id)
{
    int block  = id2block(id);
    int offset = id2offset(id);

    if (!segments[block])
        return 1;

    if (!segments[block][offset].from && !segments[block][offset].to)
        return 1;

    out->from = segments[block][offset].from;
    out->to   = segments[block][offset].to;
    return 0;
}

static int ram_ways_set(int id, struct keyval *segs, struct keyval *tags)
{
    struct keyval *p;
    int *segids;
    int segCount, i;
    int block  = id2block(id);
    int offset = id2offset(id);

    if (!ways[block]) {
        ways[block] = calloc(PER_BLOCK, sizeof(struct ramWay));
        if (!ways[block]) {
            fprintf(stderr, "Error allocating ways\n");
            exit_nicely();
        }
        way_blocks++;
        //fprintf(stderr, "\tways(%zuMb)\n", way_blocks * sizeof(struct ramWay) * PER_BLOCK / 1000000);
    }

    segCount = countList(segs);
    if (!segCount)
        return 1;

    if (ways[block][offset].segids) {
        free(ways[block][offset].segids);
        ways[block][offset].segids = NULL;
    }

    segids = malloc(sizeof(int) * (segCount+1));
    if (!segids) {
        fprintf(stderr, "%s malloc failed\n", __FUNCTION__);
        exit_nicely();
    }

    ways[block][offset].segids = segids;
    i = 0;
    while ((p = popItem(segs)) != NULL) {
        int seg_id = strtol(p->value, NULL, 10);
        freeItem(p);
        if (seg_id) /* id = 0 is used as list terminator */
            segids[i++] = seg_id;
    }
    segids[i] = 0;

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

        return 0;
}

static struct osmSegLL *getSegLL(int *segids, int *segCount)
{
    struct osmSegment segment;
    struct osmNode node;
    int id;
    int i, count = 0;
    struct osmSegLL *segll;

    while(segids[count])
        count++;

    segll = malloc(count * sizeof(struct osmSegLL));

    if (!segll) {
        *segCount = 0;
        return NULL;
    }

    count = 0;
    i = 0;
    while ((id = segids[i++]) != 0)
    {
        if (ram_segments_get(&segment, id))
            continue;

        if (ram_nodes_get(&node, segment.from))
            continue;

        segll[count].lon0 = node.lon;
        segll[count].lat0 = node.lat;

        if (ram_nodes_get(&node, segment.to))
            continue;

        segll[count].lon1 = node.lon;
        segll[count].lat1 = node.lat;

        count++;
    }
    *segCount = count;
    return segll;
}


static void ram_iterate_ways(int (*callback)(int id, struct keyval *tags, struct osmSegLL *segll, int count))
{
    int block, offset, count = 0, segCount = 0;
    struct osmSegLL *segll;

    fprintf(stderr, "\n");
    for(block=NUM_BLOCKS-1; block>=0; block--) {
        if (!ways[block])
            continue;

        for (offset=0; offset < PER_BLOCK; offset++) {
            if (ways[block][offset].segids) {
                count++;
                if (count % 1000 == 0)
                    fprintf(stderr, "\rWriting way(%uk)", count/1000);

                segll = getSegLL(ways[block][offset].segids, &segCount);

                if (segll) {
                    int id = block2id(block, offset);
                    callback(id, ways[block][offset].tags, segll, segCount);
                    free(segll);
                }

                if (ways[block][offset].tags) {
                    resetList(ways[block][offset].tags);
                    free(ways[block][offset].tags);
                    ways[block][offset].tags = NULL;
                }
                if (ways[block][offset].segids) {
                    free(ways[block][offset].segids);
                    ways[block][offset].segids = NULL;
                }
            }
        }
    }
}

static void ram_analyze(void)
{
    /* No need */
}

static void ram_end(void)
{
    /* No need */
}

static int ram_start(int dropcreate)
{
    fprintf(stderr, "passed dropcreate=%d (ignored)\n", dropcreate);
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
        if (segments[i]) {
            free(segments[i]);
            segments[i] = NULL;
        }
        if (ways[i]) {
            for (j=0; j<PER_BLOCK; j++) {
                if (ways[i][j].tags) {
                    resetList(ways[i][j].tags);
                    free(ways[i][j].tags);
                }
                if (ways[i][j].segids)
                    free(ways[i][j].segids);
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
    segments_set:   ram_segments_set,
    segments_get:   ram_segments_get,
    nodes_set:      ram_nodes_set,
    nodes_get:      ram_nodes_get,
    ways_set:       ram_ways_set,
//        ways_get:       ram_ways_get,
//        iterate_nodes:  ram_iterate_nodes,
    iterate_ways:   ram_iterate_ways
};
