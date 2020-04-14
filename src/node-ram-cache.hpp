#ifndef OSM2PGSQL_NODE_RAM_CACHE_HPP
#define OSM2PGSQL_NODE_RAM_CACHE_HPP

/* Implements the node cache in ram.
 *
 * There are two different storage strategies, either optimised
 * for dense storage of node ids, or for sparse storage as well as
 * a strategy to combine both in an optimal way.
*/

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
    void reset_used() noexcept { m_used = 0; }
    void inc_used() noexcept { ++m_used; }
    int used() const noexcept { return m_used; }

    osmium::Location *nodes = nullptr;
    int32_t block_offset = -1;

private:
    int32_t m_used = 0;
};

class node_ram_cache
{
    enum
    {
        BLOCK_SHIFT = 13
    };

public:
    /**
     * The default constructor creates a "dummy" cache which will never
     * cache anything. It is used for testing.
     */
    node_ram_cache() = default;

    node_ram_cache(int strategy, int cacheSizeMB);

    node_ram_cache(node_ram_cache const &) = delete;
    node_ram_cache &operator=(node_ram_cache const &) = delete;

    node_ram_cache(node_ram_cache &&) = delete;
    node_ram_cache &operator=(node_ram_cache &&) = delete;

    ~node_ram_cache();

    void set(osmid_t id, osmium::Location location);
    osmium::Location get(osmid_t id);

    static constexpr osmid_t per_block() noexcept
    {
        return 1ULL << BLOCK_SHIFT;
    }

private:
    static constexpr osmid_t num_blocks() noexcept
    {
        return 1ULL << (36U - BLOCK_SHIFT);
    }

    static constexpr int32_t id2block(osmid_t id) noexcept
    {
        /* allow for negative IDs */
        return (id >> BLOCK_SHIFT) + num_blocks() / 2U;
    }

    static constexpr int id2offset(osmid_t id) noexcept
    {
        return id & (per_block() - 1U);
    }

    static constexpr osmid_t block2id(int32_t block, int offset) noexcept
    {
        return (((osmid_t)block - num_blocks() / 2U) << BLOCK_SHIFT) +
               (osmid_t)offset;
    }

    void percolate_up(int pos);
    osmium::Location *next_chunk();
    void set_sparse(osmid_t id, osmium::Location location);
    void set_dense(osmid_t id, osmium::Location location);
    osmium::Location get_sparse(osmid_t id) const;
    osmium::Location get_dense(osmid_t id) const;

    int allocStrategy = 0;

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
    int64_t cacheSize = 0;
    osmid_t storedNodes = 0;
    osmid_t totalNodes = 0;
    long nodesCacheHits = 0;
    long nodesCacheLookups = 0;

    bool m_warn_node_order = true;
};

#endif // OSM2PGSQL_NODE_RAM_CACHE_HPP
