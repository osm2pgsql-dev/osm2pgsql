/* Implements the node cache in ram.
 *
 * There are two different storage strategies, either optimised
 * for dense storage of node ids, or for sparse storage as well as
 * a strategy to combine both in an optimal way.
*/

#ifndef NODE_RAM_CACHE_H
#define NODE_RAM_CACHE_H

#include "config.h"
#include "osmtypes.hpp"

#include <cstddef>
#include <climits>
#include <stdint.h>
#include <boost/noncopyable.hpp>

#define ALLOC_SPARSE 1
#define ALLOC_DENSE 2
#define ALLOC_DENSE_CHUNK 4
#define ALLOC_LOSSY 8

/// A set of coordinates, for caching in RAM or on disk
class ramNode {
public:
#ifdef FIXED_POINT
    static int scale;

    /// Default constructor creates an invalid nde
    ramNode() : _lon(INT_MIN), _lat(INT_MIN) {}
    ramNode(double lon, double lat) : _lon(dbl2fix(lon)), _lat(dbl2fix(lat)) {}

    bool is_valid() const { return _lon != INT_MIN; }
    double lon() const { return fix2dbl(_lon); }
    double lat() const { return fix2dbl(_lat); }

private:
    int _lon;
    int _lat;

    int dbl2fix(const double x) const { return x * scale + 0.4; }
    double fix2dbl(const int x) const { return (double)x / scale; }
#else
public:
    ramNode() : _lat(NAN), _lon(NAN) {}
    ramNode(double _lon, double _lat) : _lon(lon), _lat(lat) {}

    bool is_valid() const ( return !isnan(_lon); }
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

struct ramNodeBlock {
    ramNode *nodes;
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
    ramNode *next_chunk();
    int set_sparse(osmid_t id, const ramNode &coord);
    int set_dense(osmid_t id, const ramNode& coord);
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

    int scale_;

    ramNodeID *sparseBlock;
    int64_t maxSparseTuples;
    int64_t sizeSparseTuples;

    int64_t cacheUsed, cacheSize;
    osmid_t storedNodes, totalNodes;
    int nodesCacheHits, nodesCacheLookups;

    int warn_node_order;
};

#endif
