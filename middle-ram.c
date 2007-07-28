/* Implements the mid-layer processing for osm2pgsql
 * using several PostgreSQL tables
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

/* Note: these are based on a current planet.osm file + 10%, increase them if needed */
#define MAX_ID_NODE (33000000)
#define MAX_ID_SEGMENT (30000000)
#define MAX_ID_WAY (5300000)

/* Store +-180 lattitude/longittude as fixed point 32bit number with maximum precision */
/* Scale is chosen such that 360 * SCALE < 2^32          */
/* scale = 1e7 is more 'human readable',  (1<<23) is better for computers, take your pick :-) */
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
      //struct keyval tags;
};

struct ramSegment {
      int from;
      int to;
      //struct keyval tags;
};

struct ramWay {
      struct keyval *tags;
      int *segids;
};

static struct ramNode    *nodes;
static struct ramSegment *segments;
static struct ramWay     *ways;


static int ram_nodes_set(int id, double lat, double lon, struct keyval *tags)
{
    //struct keyval *p;

    if (id <= 0)
        return 1;
    if (id > MAX_ID_NODE) {
        fprintf(stderr, "Discarding node with ID(%d) > MAX(%u)\n", id, MAX_ID_NODE);
        return 1;
    }

#ifdef FIXED_POINT
    nodes[id].lat = DOUBLE_TO_FIX(lat);
    nodes[id].lon = DOUBLE_TO_FIX(lon);
#else
    nodes[id].lat = lat;
    nodes[id].lon = lon;
#endif

#if 0
    while ((p = popItem(tags)) != NULL)
        pushItem(&nodes[id].tags, p);
#else
    /* FIXME: This is a performance hack which interferes with a clean middle / output separation */
    out_pgsql.node(id, tags, lat, lon);
    resetList(tags);
#endif
    return 0;
}


static int ram_nodes_get(struct osmNode *out, int id)
{
    if (id <= 0 || id > MAX_ID_NODE)
        return 1;

    if (!nodes[id].lat && !nodes[id].lon)
        return 1;
#ifdef FIXED_POINT
    out->lat = FIX_TO_DOUBLE(nodes[id].lat);
    out->lon = FIX_TO_DOUBLE(nodes[id].lon);
#else
    out->lat = nodes[id].lat;
    out->lon = nodes[id].lon;
#endif
    return 0;
}

static int ram_segments_set(int id, int from, int to, struct keyval *tags)
{
    if (id <= 0)
        return 1;
    if (id > MAX_ID_SEGMENT) {
        fprintf(stderr, "Discarding segment with ID(%d) > MAX(%u)\n", id, MAX_ID_SEGMENT);
        return 1;
    }

    segments[id].from = from;
    segments[id].to   = to;
    resetList(tags);
    return 0;
}

static int ram_segments_get(struct osmSegment *out, int id)
{
    if (id <= 0 || id > MAX_ID_SEGMENT)
        return 1;

    if (!segments[id].from && !segments[id].to)
        return 1;

    out->from = segments[id].from;
    out->to   = segments[id].to;
    return 0;
}

static int ram_ways_set(int id, struct keyval *segs, struct keyval *tags)
{
    struct keyval *p;
    int *segids;
    int segCount, i;

    if (id <= 0)
        return 1;

    if (id > MAX_ID_WAY) {
        fprintf(stderr, "Discarding way with ID(%d) > MAX(%u)\n", id, MAX_ID_WAY);
        return 1;
    }

    segCount = countList(segs);
    if (!segCount)
        return 1;

    if (ways[id].segids) {
        free(ways[id].segids);
        ways[id].segids = NULL;
    }

    segids = malloc(sizeof(int) * (segCount+1));
    if (!segids) {
        fprintf(stderr, "%s malloc failed\n", __FUNCTION__);
        exit_nicely();
    }

    ways[id].segids = segids;
    i = 0;
    while ((p = popItem(segs)) != NULL) {
        int seg_id = strtol(p->value, NULL, 10);
        freeItem(p);
        if (seg_id) /* id = 0 is used as list terminator */
            segids[i++] = seg_id;
    }
    segids[i] = 0;
 
    if (!ways[id].tags) {
        p = malloc(sizeof(struct keyval));
        if (p) {
            initList(p);
            ways[id].tags = p;
        } else {
            fprintf(stderr, "%s malloc failed\n", __FUNCTION__);
            exit_nicely();
        }
    } else
        resetList(ways[id].tags);
 
    while ((p = popItem(tags)) != NULL)
        pushItem(ways[id].tags, p);

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
    int i, count = 0, segCount = 0;
    struct osmSegLL *segll;

    fprintf(stderr, "\n");
    for(i=MAX_ID_WAY; i>0; i--) {
        if (ways[i].segids) {
            count++;
            if (count % 1000 == 0)
                fprintf(stderr, "\rWriting way(%uk)", count/1000);

            segll = getSegLL(ways[i].segids, &segCount);

            if (segll) {
                callback(i, ways[i].tags, segll, segCount);
                free(segll);
            }

            if (ways[i].tags) {
                resetList(ways[i].tags);
                free(ways[i].tags);
                ways[i].tags = NULL;
            }
            if (ways[i].segids) {
                free(ways[i].segids);
                ways[i].segids = NULL;
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
//    int i;

   fprintf(stderr, "passed dropcreate=%d (ignored)\n", dropcreate);

    fprintf(stderr, "Allocating memory for RAM storage\n");
    if (!nodes) {
        fprintf(stderr, "\tnodes(%zuMb)\n", (MAX_ID_NODE+1) * sizeof(struct ramNode) / 1000000);
        nodes = calloc((MAX_ID_NODE+1), sizeof(struct ramNode));
        if (!nodes) {
            fprintf(stderr, "Error allocating nodes\n");
            exit_nicely();
        }
#if 0
        fprintf(stderr, "Initialising nodes\n");
        for (i=0; i<=MAX_ID_NODE; i++)
            initList(&nodes[i].tags);
#endif
    }
    if (!segments) {
        fprintf(stderr, "\tsegments(%zuMb)\n", (MAX_ID_SEGMENT+1) * sizeof(struct ramSegment) / 1000000);
        segments = calloc((MAX_ID_SEGMENT+1), sizeof(struct ramSegment));
        if (!segments) {
            fprintf(stderr, "Error allocating segments\n");
            exit_nicely();
        }
    }
    if (!ways) {
        fprintf(stderr, "\tways(%zuMb)\n", (MAX_ID_WAY+1) * sizeof(struct ramWay) / 1000000);
        ways = calloc((MAX_ID_WAY+1), sizeof(struct ramWay));
        if (!ways) {
            fprintf(stderr, "Error allocating ways\n");
            exit_nicely();
        }
    }

    return 0;
}

static void ram_stop(void)
{
    int i;

    if (nodes)
        free(nodes);

    if (segments)
        free(segments);

    if (ways) {
        for (i=0; i<MAX_ID_WAY; i++) {
            if (ways[i].tags) {
                resetList(ways[i].tags);
                free(ways[i].tags);
            }
            if (ways[i].segids)
                free(ways[i].segids);
        }
        free(ways);
    }
    nodes = NULL;
    segments = NULL;
    ways = NULL;
}
 
struct middle_t mid_ram = {
        start:          ram_start,
        stop:           ram_stop,
        end:           ram_end,
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
