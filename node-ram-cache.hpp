/* Implements the node cache in ram.
 *
 * There are two different storage strategies, either optimised
 * for dense storage of node ids, or for sparse storage as well as
 * a strategy to combine both in an optimal way.
*/

#ifndef NODE_RAM_CACHE_H
#define NODE_RAM_CACHE_H

#include <climits>
#include <cstddef>
#include <cstdint>

#include <boost/noncopyable.hpp>

#include <osmium/osm/location.hpp>

#include "osmtypes.hpp"

#define ALLOC_SPARSE 1
#define ALLOC_DENSE 2
#define ALLOC_DENSE_CHUNK 4
#define ALLOC_LOSSY 8

struct ramNodeID
{
    osmid_t id;
    osmium::Location coord;
};

class ramNodeBlock
{
public:
    ramNodeBlock() : nodes(nullptr), block_offset(-1), _used(0) {}

    void reset_used() { _used = 0; }
    void inc_used() { _used += 1; }
    int used() const { return _used; }

    osmium::Location *nodes;
    int32_t block_offset;

private:
    int32_t _used;
};

struct node_ram_cache : public boost::noncopyable
{
    node_ram_cache(int strategy, int cacheSizeMB);
    ~node_ram_cache();

    void set(osmid_t id, const osmium::Location &coord);
    osmium::Location get(osmid_t id);

private:
    void percolate_up(int pos);
    osmium::Location *next_chunk();
    void set_sparse(osmid_t id, const osmium::Location &coord);
    void set_dense(osmid_t id, const osmium::Location &coord);
    osmium::Location get_sparse(osmid_t id);
    osmium::Location get_dense(osmid_t id);

    int allocStrategy;

    ramNodeBlock *blocks;
    int usedBlocks;
    /* Note: maxBlocks *must* be odd, to make sure the priority queue has no nodes with only one child */
    int maxBlocks;
    char *blockCache;
    size_t blockCachePos;

    ramNodeBlock **queue;

    ramNodeID *sparseBlock;
    int64_t maxSparseTuples;
    int64_t sizeSparseTuples;
    osmid_t maxSparseId;

    int64_t cacheUsed, cacheSize;
    osmid_t storedNodes, totalNodes;
    long nodesCacheHits, nodesCacheLookups;

    int warn_node_order;
};

#endif
