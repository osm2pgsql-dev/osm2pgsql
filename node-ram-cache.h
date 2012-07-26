/* Implements the node cache in ram.
 * 
 * There are two different storage strategies, either optimised
 * for dense storage of node ids, or for sparse storage as well as
 * a strategy to combine both in an optimal way.
*/
 
#ifndef NODE_RAM_CACHE_H
#define NODE_RAM_CACHE_H

#define ALLOC_SPARSE 1
#define ALLOC_DENSE 2
#define ALLOC_DENSE_CHUNK 4
#define ALLOC_LOSSY 8

/* Store +-20,000km Mercator co-ordinates as fixed point 32bit number with maximum precision */
/* Scale is chosen such that 40,000 * SCALE < 2^32          */
#define FIXED_POINT
#define DOUBLE_TO_FIX(x) ((int)((x) * scale))
#define FIX_TO_DOUBLE(x) (((double)x) / scale)

#define UNUSED  __attribute__ ((unused))

struct ramNode {
#ifdef FIXED_POINT
    int lon;
    int lat;
#else
    double lon;
    double lat;
#endif
};

struct ramNodeID {
    osmid_t id;
    struct ramNode coord;
};

struct ramNodeBlock {
    struct ramNode    *nodes;
    osmid_t block_offset;
    int used;
    int dirty;
};


void init_node_ram_cache(int strategy, int cacheSizeMB, int fixpointscale);
void free_node_ram_cache();
int ram_cache_nodes_set(osmid_t id, double lat, double lon, struct keyval *tags UNUSED);
int ram_cache_nodes_get(struct osmNode *out, osmid_t id);

#endif
