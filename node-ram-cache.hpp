#ifndef OSM2PGSQL_NODE_RAM_CACHE_HPP
#define OSM2PGSQL_NODE_RAM_CACHE_HPP

/* Implements the node cache in ram.
 *
 * There are two different storage strategies, either optimised
 * for dense storage of node ids, or for sparse storage as well as
 * a strategy to combine both in an optimal way.
*/

#include <climits>
#include <cstddef>
#include <cstdint>

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
    void reset_used() { _used = 0; }
    void inc_used() { _used += 1; }
    int used() const { return _used; }

    osmium::Location *nodes = nullptr;
    int32_t block_offset = -1;

private:
    int32_t _used = 0;
};

struct node_ram_cache
{
    node_ram_cache(int strategy, int cacheSizeMB);

    node_ram_cache(node_ram_cache const &) = delete;
    node_ram_cache &operator=(node_ram_cache const &) = delete;

    node_ram_cache(node_ram_cache &&) = delete;
    node_ram_cache &operator=(node_ram_cache &&) = delete;

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

    ramNodeBlock *blocks = nullptr;
    int usedBlocks = 0;
    /* Note: maxBlocks *must* be odd, to make sure the priority queue has no nodes with only one child */
    int maxBlocks = 0;
    char *blockCache = nullptr;
    size_t blockCachePos = 0;

    ramNodeBlock **queue = nullptr;

    ramNodeID *sparseBlock = nullptr;
    int64_t maxSparseTuples = 0;
    int64_t sizeSparseTuples = 0;
    osmid_t maxSparseId = 0;

    int64_t cacheUsed = 0;
    int64_t cacheSize;
    osmid_t storedNodes = 0;
    osmid_t totalNodes = 0;
    long nodesCacheHits = 0;
    long nodesCacheLookups = 0;

    int warn_node_order = 0;
};

#endif // OSM2PGSQL_NODE_RAM_CACHE_HPP
