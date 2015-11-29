/* Implements a node cache in ram, for the middle layers to use.
 * It uses two different storage methods, one optimized for dense
 * nodes (with respect to id) and the other for sparse representations.
*/

#include "config.h"

#include <new>
#include <stdexcept>

#include <cstdio>
#include <cstdlib>

#include <boost/format.hpp>

#include "node-ram-cache.hpp"
#include "osmtypes.hpp"
#include "util.hpp"

/* Here we use a similar storage structure as middle-ram, except we allow
 * the array to be lossy so we can cap the total memory usage. Hence it is a
 * combination of a sparse array with a priority queue
 *
 * Like middle-ram we have a number of blocks all storing PER_BLOCK
 * ramNodes. However, here we also track the number of nodes in each block.
 * Seperately we have a priority queue like structure when maintains a list
 * of all the used block so we can easily find the block with the least
 * nodes. The cache has two phases:
 *
 * Phase 1: Loading initially, usedBlocks < maxBlocks. In this case when a
 * new block is needed we simply allocate it and put it in
 * queue[usedBlocks-1] which is the bottom of the tree. Every node added
 * increases it's usage. When we move onto the next block we percolate this
 * block up the queue until it reaches its correct position. The invariant
 * is that the priority tree is complete except for this last node. We do
 * not permit adding nodes to any other block to preserve this invariant.
 *
 * Phase 2: Once we've reached the maximum number of blocks permitted, we
 * change so that the block currently be inserted into is at the top of the
 * tree. When a new block is needed we take the one at the end of the queue,
 * as it is the one with the least number of nodes in it. When we move onto
 * the next block we first push the just completed block down to it's
 * correct position in the queue and then reuse the block that now at the
 * head.
 *
 * The result being that at any moment we have in memory the top maxBlock
 * blocks in terms of number of nodes in memory. This should maximize the
 * number of hits in lookups.
 *
 * Complexity:
 *  Insert node: O(1)
 *  Lookup node: O(1)
 *  Add new block: O(log usedBlocks)
 *  Reuse old block: O(log maxBlocks)
 */



#define BLOCK_SHIFT 13
#define PER_BLOCK  (((osmid_t)1) << BLOCK_SHIFT)
#define NUM_BLOCKS (((osmid_t)1) << (36 - BLOCK_SHIFT))

#define SAFETY_MARGIN 1024*PER_BLOCK*sizeof(ramNode)

#ifdef FIXED_POINT
int ramNode::scale;
#endif

static int32_t id2block(osmid_t id)
{
    /* + NUM_BLOCKS/2 allows for negative IDs */
    return (id >> BLOCK_SHIFT) + NUM_BLOCKS/2;
}

static int id2offset(osmid_t id)
{
    return id & (PER_BLOCK-1);
}

static osmid_t block2id(int32_t block, int offset)
{
    return (((osmid_t) block - NUM_BLOCKS/2) << BLOCK_SHIFT) + (osmid_t) offset;
}

#define Swap(a,b) { ramNodeBlock * __tmp = a; a = b; b = __tmp; }

void node_ram_cache::percolate_up( int pos )
{
    int i = pos;
    while( i > 0 )
    {
      int parent = (i-1)>>1;
      if( queue[i]->used() < queue[parent]->used() )
      {
        Swap( queue[i], queue[parent] )
        i = parent;
      }
      else
        break;
    }
}

ramNode *node_ram_cache::next_chunk() {
    if ( (allocStrategy & ALLOC_DENSE_CHUNK) == 0 ) {
        // allocate starting from the upper end of the block cache
        blockCachePos += PER_BLOCK * sizeof(ramNode);
        char *result = blockCache + cacheSize - blockCachePos + SAFETY_MARGIN;

        return new(result) ramNode[PER_BLOCK];
    } else {
        return new ramNode[PER_BLOCK];
    }
}


void node_ram_cache::set_sparse(osmid_t id, const ramNode &coord) {
    // Sparse cache depends on ordered nodes, reject out-of-order ids.
    // Also check that there is still space.
    if ((maxSparseId && id < maxSparseId)
         || (sizeSparseTuples > maxSparseTuples)
         || ( cacheUsed > cacheSize)) {
        if (allocStrategy & ALLOC_LOSSY) {
            return;
        } else {
            fprintf(stderr, "\nNode cache size is too small to fit all nodes. Please increase cache size\n");
            util::exit_nicely();
        }
    }
    maxSparseId = id;
    sparseBlock[sizeSparseTuples].id = id;
    sparseBlock[sizeSparseTuples].coord = coord;

    sizeSparseTuples++;
    cacheUsed += sizeof(ramNodeID);
    storedNodes++;
}

void node_ram_cache::set_dense(osmid_t id, const ramNode &coord) {
    int32_t const block  = id2block(id);
    int const offset = id2offset(id);

    if (maxBlocks == 0) {
      return;
    }

    if (!blocks[block].nodes) {
        if (((allocStrategy & ALLOC_SPARSE) > 0) && ( usedBlocks < maxBlocks) && ( cacheUsed > cacheSize)) {
            /* TODO: It is more memory efficient to drop nodes from the sparse node cache than from the dense node cache */
        }
        if ((usedBlocks < maxBlocks ) && (cacheUsed < cacheSize)) {
            /* if usedBlocks > 0 then the previous block is used up. Need to correctly handle it. */
            if ( usedBlocks > 0 ) {
                /* If sparse allocation is also set, then check if the previous block has sufficient density
                 * to store it in dense representation. If not, push all elements of the block
                 * to the sparse node cache and reuse memory of the previous block for the current block */
                if ( ((allocStrategy & ALLOC_SPARSE) == 0) ||
                     ((queue[usedBlocks - 1]->used() / (double)(1<< BLOCK_SHIFT)) >
                      (sizeof(ramNode) / (double)sizeof(ramNodeID)))) {
                    /* Block has reached the level to keep it in dense representation */
                    /* We've just finished with the previous block, so we need to percolate it up the queue to its correct position */
                    /* Upto log(usedBlocks) iterations */
                    percolate_up( usedBlocks-1 );
                    blocks[block].nodes = next_chunk();
                } else {
                    /* previous block was not dense enough, so push it into the sparse node cache instead */
                    for (int i = 0; i < (1 << BLOCK_SHIFT); i++) {
                        if (queue[usedBlocks -1]->nodes[i].is_valid()) {
                            set_sparse(block2id(queue[usedBlocks - 1]->block_offset, i),
                                       queue[usedBlocks -1]->nodes[i]);
                            queue[usedBlocks -1]->nodes[i] = ramNode(); // invalidate
                        }
                    }
                    /* reuse previous block, as its content is now in the sparse representation */
                    storedNodes -= queue[usedBlocks - 1]->used();
                    blocks[block].nodes = queue[usedBlocks - 1]->nodes;
                    blocks[queue[usedBlocks - 1]->block_offset].nodes = nullptr;
                    usedBlocks--;
                    cacheUsed -= PER_BLOCK * sizeof(ramNode);
                }
            } else {
                blocks[block].nodes = next_chunk();
            }

            blocks[block].reset_used();
            blocks[block].block_offset = block;
            if (!blocks[block].nodes) {
                fprintf(stderr, "Error allocating nodes\n");
                util::exit_nicely();
            }
            queue[usedBlocks] = &blocks[block];
            usedBlocks++;
            cacheUsed += PER_BLOCK * sizeof(ramNode);

            /* If we've just used up the last possible block we enter the
             * transition and we change the invariant. To do this we percolate
             * the newly allocated block straight to the head */
            if (( usedBlocks == maxBlocks ) || ( cacheUsed > cacheSize ))
                percolate_up( usedBlocks-1 );
        } else {
            if ((allocStrategy & ALLOC_LOSSY) == 0) {
                fprintf(stderr, "\nNode cache size is too small to fit all nodes. Please increase cache size\n");
                util::exit_nicely();
            }
            /* We've reached the maximum number of blocks, so now we push the
             * current head of the tree down to the right level to restore the
             * priority queue invariant. Upto log(maxBlocks) iterations */

            int i = 0;
            while( 2*i+1 < usedBlocks - 1 ) {
                if( queue[2*i+1]->used() <= queue[2*i+2]->used() ) {
                    if( queue[i]->used() > queue[2*i+1]->used() ) {
                        Swap( queue[i], queue[2*i+1] );
                        i = 2*i+1;
                    }
                    else
                        break;
                } else {
                    if( queue[i]->used() > queue[2*i+2]->used() ) {
                        Swap( queue[i], queue[2*i+2] );
                        i = 2*i+2;
                    } else
                        break;
                }
            }
            /* Now the head of the queue is the smallest, so it becomes our replacement candidate */
            blocks[block].nodes = queue[0]->nodes;
            blocks[block].reset_used();
            new(blocks[block].nodes) ramNode[PER_BLOCK];

            /* Clear old head block and point to new block */
            storedNodes -= queue[0]->used();
            queue[0]->nodes = nullptr;
            queue[0]->reset_used();
            queue[0] = &blocks[block];
        }
    } else {
        /* Insert into an existing block. We can't allow this in general or it
         * will break the invariant. However, it will work fine if all the
         * nodes come in numerical order, which is the common case */

        int expectedpos;
        if (( usedBlocks < maxBlocks ) && (cacheUsed < cacheSize))
            expectedpos = usedBlocks-1;
        else
            expectedpos = 0;

        if( queue[expectedpos] != &blocks[block] ) {
            if (!warn_node_order) {
                fprintf( stderr, "WARNING: Found Out of order node %" PRIdOSMID " (%d,%d) - this will impact the cache efficiency\n", id, block, offset );
                warn_node_order++;
            }
            return;
        }
    }

    blocks[block].nodes[offset] = coord;
    blocks[block].inc_used();
    storedNodes++;
}


int node_ram_cache::get_sparse(osmNode *out, osmid_t id) {
    int64_t pivotPos = sizeSparseTuples >> 1;
    int64_t minPos = 0;
    int64_t maxPos = sizeSparseTuples;

    while (minPos <= maxPos) {
        if ( sparseBlock[pivotPos].id == id ) {
            out->lat = sparseBlock[pivotPos].coord.lat();
            out->lon = sparseBlock[pivotPos].coord.lon();
            return 0;
        }
        if ( (pivotPos == minPos) || (pivotPos == maxPos)) return 1;

        if ( sparseBlock[pivotPos].id > id ) {
            maxPos = pivotPos;
            pivotPos = minPos + ((maxPos - minPos) >> 1);
        } else {
            minPos = pivotPos;
            pivotPos = minPos + ((maxPos - minPos) >> 1);
        }
    }

    return 1;
}

int node_ram_cache::get_dense(osmNode *out, osmid_t id) {
    int32_t const block  = id2block(id);
    int const offset = id2offset(id);

    if (!blocks[block].nodes)
        return 1;

    if (!blocks[block].nodes[offset].is_valid())
        return 1;

    out->lat = blocks[block].nodes[offset].lat();
    out->lon = blocks[block].nodes[offset].lon();

    return 0;
}


node_ram_cache::node_ram_cache( int strategy, int cacheSizeMB, int fixpointscale )
    : allocStrategy(ALLOC_DENSE), blocks(nullptr), usedBlocks(0),
      maxBlocks(0), blockCache(nullptr), queue(nullptr), sparseBlock(nullptr),
      maxSparseTuples(0), sizeSparseTuples(0), maxSparseId(0), cacheUsed(0),
      cacheSize(0), storedNodes(0), totalNodes(0), nodesCacheHits(0),
      nodesCacheLookups(0), warn_node_order(0) {
#ifdef FIXED_POINT
    ramNode::scale = fixpointscale;
#endif
    blockCache = 0;
    blockCachePos = 0;
    cacheUsed = 0;
    cacheSize = (int64_t)cacheSizeMB*(1024*1024);
    /* How much we can fit, and make sure it's odd */
    maxBlocks = (cacheSize/(PER_BLOCK*sizeof(ramNode)));
    maxSparseTuples = (cacheSize/sizeof(ramNodeID))+1;

    allocStrategy = strategy;

    if ((allocStrategy & ALLOC_DENSE) > 0 ) {
        fprintf(stderr, "Allocating memory for dense node cache\n");
        blocks = (ramNodeBlock *)calloc(NUM_BLOCKS,sizeof(ramNodeBlock));
        if (!blocks) {
            fprintf(stderr, "Out of memory for node cache dense index, try using \"--cache-strategy sparse\" instead \n");
            util::exit_nicely();
        }
        queue = (ramNodeBlock **)calloc( maxBlocks,sizeof(ramNodeBlock *) );
        /* Use this method of allocation if virtual memory is limited,
         * or if OS allocs physical memory right away, rather than page by page
         * once it is needed.
         */
        if( (allocStrategy & ALLOC_DENSE_CHUNK) > 0 ) {
            fprintf(stderr, "Allocating dense node cache in block sized chunks\n");
            if (!queue) {
                fprintf(stderr, "Out of memory, reduce --cache size\n");
                util::exit_nicely();
            }
        } else {
            fprintf(stderr, "Allocating dense node cache in one big chunk\n");
            blockCache = (char *)malloc((maxBlocks + 1024) * PER_BLOCK * sizeof(ramNode));
            if (!queue || !blockCache) {
                fprintf(stderr, "Out of memory for dense node cache, reduce --cache size\n");
                util::exit_nicely();
            }
        }
    }

    /* Allocate the full amount of memory given by --cache parameter in one go.
     * If both dense and sparse cache alloc is set, this will allocate up to twice
     * as much virtual memory as specified by --cache. This relies on the OS doing
     * lazy allocation of physical RAM. Extra accounting during setting of nodes is done
     * to ensure physical RAM usage should roughly be no more than --cache
     */

    if ((allocStrategy & ALLOC_SPARSE) > 0 ) {
        fprintf(stderr, "Allocating memory for sparse node cache\n");
        if (!blockCache) {
            sparseBlock = (ramNodeID *)malloc(maxSparseTuples * sizeof(ramNodeID));
        } else {
            fprintf(stderr, "Sharing dense sparse\n");
            sparseBlock = (ramNodeID *)blockCache;
        }
        if (!sparseBlock) {
            fprintf(stderr, "Out of memory for sparse node cache, reduce --cache size\n");
            util::exit_nicely();
        }
    }

    fprintf( stderr, "Node-cache: cache=%" PRId64 "MB, maxblocks=%d*%" PRId64 ", allocation method=%i\n", (cacheSize >> 20), maxBlocks, (int64_t) PER_BLOCK*sizeof(ramNode), allocStrategy );
}

node_ram_cache::~node_ram_cache() {
  fprintf( stderr, "node cache: stored: %" PRIdOSMID "(%.2f%%), storage efficiency: %.2f%% (dense blocks: %i, sparse nodes: %" PRId64 "), hit rate: %.2f%%\n",
           storedNodes, 100.0f*storedNodes/totalNodes, 100.0f*storedNodes*sizeof(ramNode)/cacheUsed,
           usedBlocks, sizeSparseTuples,
           100.0f*nodesCacheHits/nodesCacheLookups );

  if ( (allocStrategy & ALLOC_DENSE) > 0 ) {
      if ( (allocStrategy & ALLOC_DENSE_CHUNK) > 0 ) {
          for(int i = 0; i < usedBlocks; ++i) {
              delete[] queue[i]->nodes;
              queue[i]->nodes = nullptr;
          }
      } else {
          free(blockCache);
          blockCache = 0;
      }
      free(blocks);
      free(queue);
  }
  if ( ((allocStrategy & ALLOC_SPARSE) > 0) && ((allocStrategy & ALLOC_DENSE) == 0)) {
      free(sparseBlock);
  }
}

void node_ram_cache::set(osmid_t id, double lat, double lon, const taglist_t &) {
    if ((id > 0 && id >> BLOCK_SHIFT >> 32) || (id < 0 && ~id >> BLOCK_SHIFT >> 32 )) {
        fprintf(stderr, "\nAbsolute node IDs must not be larger than %" PRId64 " (got%" PRId64 " )\n",
                (int64_t) 1 << 42, (int64_t) id);
        util::exit_nicely();
    }
    totalNodes++;
    /* if ALLOC_DENSE and ALLOC_SPARSE are set, send it through
     * ram_nodes_set_dense. If a block is non dense, it will automatically
     * get pushed to the sparse cache if a block is sparse and ALLOC_SPARSE is set
     */
    if ( (allocStrategy & ALLOC_DENSE) > 0 ) {
        set_dense(id, ramNode(lon, lat));
    } else if ( (allocStrategy & ALLOC_SPARSE) > 0 ) {
        set_sparse(id, ramNode(lon, lat));
    } else {
        // Command line options always have ALLOC_DENSE | ALLOC_SPARSE
        throw std::logic_error((boost::format("Unexpected cache strategy in node_ram_cache::set with allocStrategy %1%") % allocStrategy).str());
    }
}

int node_ram_cache::get(osmNode *out, osmid_t id) {
    nodesCacheLookups++;

    if ((allocStrategy & ALLOC_DENSE) > 0) {
        if (get_dense(out, id) == 0) {
            nodesCacheHits++;
            return 0;
        }
    }
    if ((allocStrategy & ALLOC_SPARSE) > 0) {
        if (get_sparse(out, id) == 0) {
            nodesCacheHits++;
            return 0;
        }
    }

    return 1;
}
