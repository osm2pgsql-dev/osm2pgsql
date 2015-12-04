/* Implements the node cache in ram.
 *
 * There are two different storage strategies, either optimised
 * for dense storage of node ids, or for sparse storage as well as
 * a strategy to combine both in an optimal way.
*/

#ifndef NODE_RAM_CACHE_H
#define NODE_RAM_CACHE_H

#include "config.h"

#include <climits>
#include <cstddef>
#include <cstdint>

#include <boost/noncopyable.hpp>

#include "osmtypes.hpp"

#define ALLOC_SPARSE 1
#define ALLOC_DENSE 2
#define ALLOC_DENSE_CHUNK 4
#define ALLOC_LOSSY 8

/**
 * A set of coordinates, for caching in RAM or on disk.
 *
 * If FIXED_POINT is enabled, it uses internally a more efficient
 * representation as integer.
 */
class ramNode {
public:
#ifdef FIXED_POINT
    static int scale;

    /// Default constructor creates an invalid node
    ramNode() : _lon(INT_MIN), _lat(INT_MIN) {}
    /**
     * Standard constructor takes geographic coordinates and saves them
     * in the internal node representation.
     */
    ramNode(double lon, double lat) : _lon(dbl2fix(lon)), _lat(dbl2fix(lat)) {}
    /**
     * Internal constructor which takes already encoded nodes.
     *
     * Used by middle-pgsql which stores encoded nodes in the DB.
     */
    ramNode(int lon, int lat) : _lon(lon), _lat(lat) {}

    /// Return true if the node currently stores valid coordinates.
    bool is_valid() const { return _lon != INT_MIN; }
    /// Return longitude (converting from internal representation)
    double lon() const { return fix2dbl(_lon); }
    /// Return latitude (converting from internal representation)
    double lat() const { return fix2dbl(_lat); }
    /// Return internal representation of longitude (for external storage).
    int int_lon() const { return _lon; }
    /// Return internal representation of latitude (for external storage).
    int int_lat() const { return _lat; }

private:
    int _lon;
    int _lat;

    int dbl2fix(const double x) const { return (int) (x * scale + 0.4); }
    double fix2dbl(const int x) const { return (double)x / scale; }
#else
public:
    ramNode() : _lat(NAN), _lon(NAN) {}
    ramNode(double lon, double lat) : _lon(lon), _lat(lat) {}

    bool is_valid() const { return !std::isnan(_lon); }
    double lon() const { return _lon; }
    double lat() const { return _lat; }
private:
    double _lon;
    double _lat;

#endif
};

struct ramNodeID {
    osmid_t id;
    ramNode coord;
};

class ramNodeBlock {
public:
    ramNodeBlock() : nodes(nullptr), block_offset(-1), _used(0) {}

    void set_dirty() { _used |= 1; }
    bool dirty() const { return _used & 1; }

    void reset_used() { _used = 0; }
    void inc_used() { _used += 2; }
    void dec_used() { _used -= 2; }
    void set_used(int used) { _used = (used << 1) || (_used & 1); }
    int used() const { return _used >> 1; }

    ramNode *nodes;
    int32_t block_offset;
private:
    int32_t _used; // 0-bit indicates dirty
};

struct node_ram_cache : public boost::noncopyable
{
    node_ram_cache(int strategy, int cacheSizeMB, int fixpointscale);
    ~node_ram_cache();

    void set(osmid_t id, double lat, double lon, const taglist_t &tags);
    int get(osmNode *out, osmid_t id);

private:
    void percolate_up( int pos );
    ramNode *next_chunk();
    void set_sparse(osmid_t id, const ramNode &coord);
    void set_dense(osmid_t id, const ramNode& coord);
    int get_sparse(osmNode *out, osmid_t id);
    int get_dense(osmNode *out, osmid_t id);

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
    int nodesCacheHits, nodesCacheLookups;

    int warn_node_order;
};

#endif
