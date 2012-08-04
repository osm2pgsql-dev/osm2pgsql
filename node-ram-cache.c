/* Implements a node cache in ram, for the middle layers to use.
 * It uses two different storage methods, one optimized for dense
 * nodes (with respect to id) and the other for sparse representations.
*/
 
#include "config.h"

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "osmtypes.h"
#include "middle.h"
#include "node-ram-cache.h"






static int scale = 100;
#define DOUBLE_TO_FIX(x) ((int)((x) * scale))
#define FIX_TO_DOUBLE(x) (((double)x) / scale)

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



static int allocStrategy = ALLOC_DENSE;

#define BLOCK_SHIFT 10
#define PER_BLOCK  (((osmid_t)1) << BLOCK_SHIFT)
#define NUM_BLOCKS (((osmid_t)1) << (36 - BLOCK_SHIFT))

#define SAFETY_MARGIN 1024*PER_BLOCK*sizeof(struct ramNode)

static struct ramNodeBlock *blocks;
static int usedBlocks;
/* Note: maxBlocks *must* be odd, to make sure the priority queue has no nodes with only one child */
static int maxBlocks = 0;
static void *blockCache = NULL;

static struct ramNodeBlock **queue;


static struct ramNodeID *sparseBlock;
static int64_t maxSparseTuples = 0;
static int64_t sizeSparseTuples = 0;


static int64_t cacheUsed, cacheSize;
static osmid_t storedNodes, totalNodes;
int nodesCacheHits, nodesCacheLookups;

static int warn_node_order;

static int ram_cache_nodes_get_sparse(struct osmNode *out, osmid_t id);

static inline int id2block(osmid_t id)
{
    // + NUM_BLOCKS/2 allows for negative IDs
    return (id >> BLOCK_SHIFT) + NUM_BLOCKS/2;
}

static inline int id2offset(osmid_t id)
{
    return id & (PER_BLOCK-1);
}

static inline osmid_t block2id(int block, int offset)
{
    return (((osmid_t) block - NUM_BLOCKS/2) << BLOCK_SHIFT) + (osmid_t) offset;
}

#define Swap(a,b) { typeof(a) __tmp = a; a = b; b = __tmp; }

static void percolate_up( int pos )
{
    int i = pos;
    while( i > 0 )
    {
      int parent = (i-1)>>1;
      if( queue[i]->used < queue[parent]->used )
      {
        Swap( queue[i], queue[parent] );
        i = parent;
      }
      else
        break;
    }
}

static void *next_chunk(size_t count, size_t size) {
    if ( (allocStrategy & ALLOC_DENSE_CHUNK) == 0 ) {
        static size_t pos = 0;
        void *result;
        pos += count * size;
        result = blockCache + cacheSize - pos + SAFETY_MARGIN;
        
        return result;
    } else {
        return calloc(PER_BLOCK, sizeof(struct ramNode));
    }
}


static int ram_cache_nodes_set_sparse(osmid_t id, double lat, double lon, struct keyval *tags UNUSED) {
    if ((sizeSparseTuples > maxSparseTuples) || ( cacheUsed > cacheSize)) {
        if ((allocStrategy & ALLOC_LOSSY) > 0)
            return 1;
        else {
            fprintf(stderr, "\nNode cache size is too small to fit all nodes. Please increase cache size\n");
            exit_nicely();
        }
    }
    sparseBlock[sizeSparseTuples].id = id;
#ifdef FIXED_POINT
    sparseBlock[sizeSparseTuples].coord.lat = DOUBLE_TO_FIX(lat);
    sparseBlock[sizeSparseTuples].coord.lon = DOUBLE_TO_FIX(lon);
#else
    sparseBlock[sizeSparseTuples].coord.lat = lat;
    sparseBlock[sizeSparseTuples].coord.lon = lon;
#endif
    sizeSparseTuples++;
    cacheUsed += sizeof(struct ramNodeID);
    storedNodes++;
    return 0;
}

static int ram_cache_nodes_set_dense(osmid_t id, double lat, double lon, struct keyval *tags UNUSED) {
    int block  = id2block(id);
    int offset = id2offset(id);
    int i = 0;

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
                     ((queue[usedBlocks - 1]->used / (double)(1<< BLOCK_SHIFT)) > 
                      (sizeof(struct ramNode) / (double)sizeof(struct ramNodeID)))) {
                    /* Block has reached the level to keep it in dense representation */
                    /* We've just finished with the previous block, so we need to percolate it up the queue to its correct position */
                    /* Upto log(usedBlocks) iterations */
                    percolate_up( usedBlocks-1 );
                    blocks[block].nodes = next_chunk(PER_BLOCK, sizeof(struct ramNode));
                } else {
                    /* previous block was not dense enough, so push it into the sparse node cache instead */
                    for (i = 0; i < (1 << BLOCK_SHIFT); i++) {
                        if (queue[usedBlocks -1]->nodes[i].lat || queue[usedBlocks -1]->nodes[i].lon) {
                            ram_cache_nodes_set_sparse(block2id(queue[usedBlocks - 1]->block_offset,i), 
#ifdef FIXED_POINT
                                                       FIX_TO_DOUBLE(queue[usedBlocks -1]->nodes[i].lat),
                                                       FIX_TO_DOUBLE(queue[usedBlocks -1]->nodes[i].lon), 
#else
                                                       queue[usedBlocks -1]->nodes[i].lat,
                                                       queue[usedBlocks -1]->nodes[i].lon, 
#endif
                                                       NULL);
                        }
                    }
                    /* reuse previous block, as it's content is now in the dense representation */
                    storedNodes -= queue[usedBlocks - 1]->used;
                    blocks[block].nodes = queue[usedBlocks - 1]->nodes;
                    blocks[queue[usedBlocks - 1]->block_offset].nodes = NULL;
                    memset( blocks[block].nodes, 0, PER_BLOCK * sizeof(struct ramNode) );
                    usedBlocks--;                    
                    cacheUsed -= PER_BLOCK * sizeof(struct ramNode);
                }
            } else {
                blocks[block].nodes = next_chunk(PER_BLOCK, sizeof(struct ramNode));
            }
            
            blocks[block].used = 0;
            blocks[block].block_offset = block;
            if (!blocks[block].nodes) {
                fprintf(stderr, "Error allocating nodes\n");
                exit_nicely();
            }
            queue[usedBlocks] = &blocks[block];
            usedBlocks++;
            cacheUsed += PER_BLOCK * sizeof(struct ramNode);
            

            /* If we've just used up the last possible block we enter the
             * transition and we change the invariant. To do this we percolate
             * the newly allocated block straight to the head */
            if (( usedBlocks == maxBlocks ) || ( cacheUsed > cacheSize ))
                percolate_up( usedBlocks-1 );
        } else {
            if ((allocStrategy & ALLOC_LOSSY) == 0) {
                fprintf(stderr, "\nNode cache size is too small to fit all nodes. Please increase cache size\n");
                exit_nicely();
            }
            /* We've reached the maximum number of blocks, so now we push the
             * current head of the tree down to the right level to restore the
             * priority queue invariant. Upto log(maxBlocks) iterations */
            
            int i=0;
            while( 2*i+1 < usedBlocks - 1 ) {
                if( queue[2*i+1]->used <= queue[2*i+2]->used ) {
                    if( queue[i]->used > queue[2*i+1]->used ) {
                        Swap( queue[i], queue[2*i+1] );
                        i = 2*i+1;
                    }
                    else
                        break;
                } else {
                    if( queue[i]->used > queue[2*i+2]->used ) {
                        Swap( queue[i], queue[2*i+2] );
                        i = 2*i+2;
                    } else
                        break;
                }
            }
            /* Now the head of the queue is the smallest, so it becomes our replacement candidate */
            blocks[block].nodes = queue[0]->nodes;
            blocks[block].used = 0;
            memset( blocks[block].nodes, 0, PER_BLOCK * sizeof(struct ramNode) );
            
            /* Clear old head block and point to new block */
            storedNodes -= queue[0]->used;
            queue[0]->nodes = NULL;
            queue[0]->used = 0;
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
            return 1;
        }
    }
    
#ifdef FIXED_POINT
    blocks[block].nodes[offset].lat = DOUBLE_TO_FIX(lat);
    blocks[block].nodes[offset].lon = DOUBLE_TO_FIX(lon);
#else
    blocks[block].nodes[offset].lat = lat;
    blocks[block].nodes[offset].lon = lon;
#endif
    blocks[block].used++;
    storedNodes++;
    return 0;
}


static int ram_cache_nodes_get_sparse(struct osmNode *out, osmid_t id) {
    int64_t pivotPos = sizeSparseTuples >> 1;
    int64_t minPos = 0;
    int64_t maxPos = sizeSparseTuples;

    while (minPos <= maxPos) {
        if ( sparseBlock[pivotPos].id == id ) {
#ifdef FIXED_POINT
            out->lat = FIX_TO_DOUBLE(sparseBlock[pivotPos].coord.lat);
            out->lon = FIX_TO_DOUBLE(sparseBlock[pivotPos].coord.lon);
#else
            out->lat = sparseBlock[pivotPos].coord.lat;
            out->lon = sparseBlock[pivotPos].coord.lon;
#endif
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

static int ram_cache_nodes_get_dense(struct osmNode *out, osmid_t id) {
    int block  = id2block(id);
    int offset = id2offset(id);
    
    if (!blocks[block].nodes)
        return 1;
    
    if (!blocks[block].nodes[offset].lat && !blocks[block].nodes[offset].lon)
        return 1;
    
#ifdef FIXED_POINT
    out->lat = FIX_TO_DOUBLE(blocks[block].nodes[offset].lat);
    out->lon = FIX_TO_DOUBLE(blocks[block].nodes[offset].lon);
#else
    out->lat = blocks[block].nodes[offset].lat;
    out->lon = blocks[block].nodes[offset].lon;
#endif
    
    return 0;
}


void init_node_ram_cache( int strategy, int cacheSizeMB, int fixpointscale ) {

    blockCache = 0;
    cacheUsed = 0;
    cacheSize = (int64_t)cacheSizeMB*(1024*1024);
    /* How much we can fit, and make sure it's odd */
    maxBlocks = (cacheSize/(PER_BLOCK*sizeof(struct ramNode))) | 1;
    maxSparseTuples = (cacheSize/sizeof(struct ramNodeID)) | 1;
    
    allocStrategy = strategy;
    scale = fixpointscale;
    
    if ((allocStrategy & ALLOC_DENSE) > 0 ) {
        fprintf(stderr, "Allocating memory for dense node cache\n");
        blocks = calloc(NUM_BLOCKS,sizeof(struct ramNodeBlock));
        if (!blocks) {
            fprintf(stderr, "Out of memory for node cache dense index, try using \"--cache-strategy sparse\" instead \n");
            exit_nicely();
        }
        queue = calloc( maxBlocks,sizeof(struct ramNodeBlock) );
        /* Use this method of allocation if virtual memory is limited,
         * or if OS allocs physical memory right away, rather than page by page
         * once it is needed.
         */
        if( (allocStrategy & ALLOC_DENSE_CHUNK) > 0 ) {
            fprintf(stderr, "Allocating dense node cache in block sized chunks\n");
            if (!queue) {
                fprintf(stderr, "Out of memory, reduce --cache size\n");
                exit_nicely();
            }
        } else {
            fprintf(stderr, "Allocating dense node cache in one big chunk\n");
            blockCache = calloc(maxBlocks + 1024,PER_BLOCK * sizeof(struct ramNode));
            if (!queue || !blockCache) {
                fprintf(stderr, "Out of memory for dense node cache, reduce --cache size\n");
                exit_nicely();
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
            sparseBlock = calloc(maxSparseTuples,sizeof(struct ramNodeID));
        } else {
            fprintf(stderr, "Sharing dense sparse\n");
            sparseBlock = blockCache;
        }
        if (!sparseBlock) {
            fprintf(stderr, "Out of memory for sparse node cache, reduce --cache size\n");
            exit_nicely();
        }
    }

#ifdef __MINGW_H
    fprintf( stderr, "Node-cache: cache=%ldMB, maxblocks=%d*%d, allocation method=%i\n", (cacheSize >> 20), maxBlocks, PER_BLOCK*sizeof(struct ramNode), allocStrategy ); 
#else
    fprintf( stderr, "Node-cache: cache=%ldMB, maxblocks=%d*%zd, allocation method=%i\n", (cacheSize >> 20), maxBlocks, PER_BLOCK*sizeof(struct ramNode), allocStrategy );
#endif
}

void free_node_ram_cache() {
  int i;
  fprintf( stderr, "node cache: stored: %" PRIdOSMID "(%.2f%%), storage efficiency: %.2f%% (dense blocks: %i, sparse nodes: %li), hit rate: %.2f%%\n", 
           storedNodes, 100.0f*storedNodes/totalNodes, 100.0f*storedNodes*sizeof(struct ramNode)/cacheUsed,
           usedBlocks, sizeSparseTuples,
           100.0f*nodesCacheHits/nodesCacheLookups );

  if ( (allocStrategy & ALLOC_DENSE) > 0 ) {
      if ( (allocStrategy & ALLOC_DENSE_CHUNK) > 0 ) {
          for( i=0; i<usedBlocks; i++ ) {
              free(queue[i]->nodes);
              queue[i]->nodes = NULL;
          }
      } else {
          free(blockCache);
          blockCache = 0;
      }
      free(queue);
  }
  if ( ((allocStrategy & ALLOC_SPARSE) > 0) && ((allocStrategy & ALLOC_DENSE) == 0)) {
      free(sparseBlock);
  }
}

int ram_cache_nodes_set(osmid_t id, double lat, double lon, struct keyval *tags UNUSED) {
    totalNodes++;
    /* if ALLOC_DENSE and ALLOC_SPARSE are set, send it through 
     * ram_nodes_set_dense. If a block is non dense, it will automatically
     * get pushed to the sparse cache if a block is sparse and ALLOC_SPARSE is set
     */
    if ( (allocStrategy & ALLOC_DENSE) > 0 ) {
        return ram_cache_nodes_set_dense(id, lat, lon, tags);
    }
    if ( (allocStrategy & ALLOC_SPARSE) > 0 )
        return ram_cache_nodes_set_sparse(id, lat, lon, tags);
    return 1;
}

int ram_cache_nodes_get(struct osmNode *out, osmid_t id) {
    nodesCacheLookups++;

    if ((allocStrategy & ALLOC_DENSE) > 0) {
        if (ram_cache_nodes_get_dense(out,id) == 0) {
            nodesCacheHits++;
            return 0;        
        }
    }
    if ((allocStrategy & ALLOC_SPARSE) > 0) {
        if (ram_cache_nodes_get_sparse(out,id) == 0) {
            nodesCacheHits++;
            return 0;        
        }
    }

    return 1;
}
