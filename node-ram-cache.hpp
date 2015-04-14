/* Implements the node cache in ram.
 *
 * There are two different storage strategies, either optimised
 * for dense storage of node ids, or for sparse storage as well as
 * a strategy to combine both in an optimal way.
*/

#ifndef NODE_RAM_CACHE_H
#define NODE_RAM_CACHE_H

#include "osmtypes.hpp"

#include <boost/noncopyable.hpp>

#define ALLOC_SPARSE 1
#define ALLOC_DENSE 2
#define ALLOC_DENSE_CHUNK 4
#define ALLOC_LOSSY 8

/* Store +-20,000km Mercator co-ordinates as fixed point 32bit number with maximum precision */
#define FIXED_POINT

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

struct node_ram_cache : public boost::noncopyable
{
    node_ram_cache(int strategy, int cacheSizeMB, int fixpointscale);
    ~node_ram_cache();

    int set(osmid_t id, double lat, double lon, const taglist_t &tags);
    int get(osmNode *out, osmid_t id);

private:
    void percolate_up( int pos );
    struct ramNode *next_chunk(size_t count, size_t size);
    int set_sparse(osmid_t id, double lat, double lon);
    int set_dense(osmid_t id, double lat, double lon);
    int get_sparse(osmNode *out, osmid_t id);
    int get_dense(osmNode *out, osmid_t id);

    int allocStrategy;

    struct ramNodeBlock *blocks;
    int usedBlocks;
    /* Note: maxBlocks *must* be odd, to make sure the priority queue has no nodes with only one child */
    int maxBlocks;
    char *blockCache;

    struct ramNodeBlock **queue;

    int scale_;

    struct ramNodeID *sparseBlock;
    int64_t maxSparseTuples;
    int64_t sizeSparseTuples;

    int64_t cacheUsed, cacheSize;
    osmid_t storedNodes, totalNodes;
    int nodesCacheHits, nodesCacheLookups;

    int warn_node_order;
};

#endif
