/* Implements a node cache in ram, for the middle layers to use.
 * It uses two different storage methods, one optimized for dense
 * nodes (with respect to id) and the other for sparse representations.
*/

#include "format.hpp"
#include "logging.hpp"
#include "node-ram-cache.hpp"
#include "osmtypes.hpp"
#include "util.hpp"

#include <cstdio>
#include <cstdlib>
#include <new>
#include <stdexcept>

/* Here we use a similar storage structure as middle-ram, except we allow
 * the array to be lossy so we can cap the total memory usage. Hence it is a
 * combination of a sparse array with a priority queue.
 *
 * Like middle-ram we have a number of blocks all storing per_block()
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

void node_ram_cache::percolate_up(int pos)
{
    while (pos > 0) {
        int const parent = (pos - 1) >> 1U;
        if (queue[pos]->used() < queue[parent]->used()) {
            using std::swap;
            swap(queue[pos], queue[parent]);
            pos = parent;
        } else {
            break;
        }
    }
}

osmium::Location *node_ram_cache::next_chunk()
{
    constexpr auto const safety_margin =
        1024 * per_block() * sizeof(osmium::Location);

    if ((allocStrategy & ALLOC_DENSE_CHUNK) == 0) {
        // allocate starting from the upper end of the block cache
        blockCachePos += per_block() * sizeof(osmium::Location);
        char *result = blockCache + cacheSize - blockCachePos + safety_margin;

        return new (result) osmium::Location[per_block()];
    }

    return new osmium::Location[per_block()];
}

void node_ram_cache::set_sparse(osmid_t id, osmium::Location location)
{
    // Sparse cache depends on ordered nodes, reject out-of-order ids.
    // Also check that there is still space.
    if ((maxSparseId && id < maxSparseId) ||
        (sizeSparseTuples > maxSparseTuples) || (cacheUsed > cacheSize)) {
        if (allocStrategy & ALLOC_LOSSY) {
            return;
        } else {
            if (maxSparseId && id < maxSparseId) {
                throw std::runtime_error{
                    "Node ids are out of order. Please use slim mode."};
            }
            throw std::runtime_error{
                "Node cache size is too small to fit all nodes. Please "
                "increase cache size."};
        }
    }
    maxSparseId = id;
    sparseBlock[sizeSparseTuples].id = id;
    sparseBlock[sizeSparseTuples].coord = location;

    ++sizeSparseTuples;
    cacheUsed += sizeof(ramNodeID);
    ++storedNodes;
}

void node_ram_cache::set_dense(osmid_t id, osmium::Location location)
{
    int32_t const block = id2block(id);
    assert(
        block <
        num_blocks()); // https://github.com/openstreetmap/osm2pgsql/issues/965
    int const offset = id2offset(id);

    if (maxBlocks == 0) {
        return;
    }

    if (!blocks[block].nodes) {
        if (((allocStrategy & ALLOC_SPARSE) > 0) && (usedBlocks < maxBlocks) &&
            (cacheUsed > cacheSize)) {
            /* TODO: It is more memory efficient to drop nodes from the sparse node
             * cache than from the dense node cache */
        }
        if ((usedBlocks < maxBlocks) && (cacheUsed < cacheSize)) {
            /* if usedBlocks > 0 then the previous block is used up. Need to correctly
             * handle it. */
            if (usedBlocks > 0) {
                /* If sparse allocation is also set, then check if the previous block
                 * has sufficient density
                 * to store it in dense representation. If not, push all elements of the
                 * block
                 * to the sparse node cache and reuse memory of the previous block for
                 * the current block */
                if (((allocStrategy & ALLOC_SPARSE) == 0) ||
                    ((queue[usedBlocks - 1]->used() /
                      (double)(1 << BLOCK_SHIFT)) >
                     (sizeof(osmium::Location) / (double)sizeof(ramNodeID)))) {
                    /* Block has reached the level to keep it in dense representation */
                    /* We've just finished with the previous block, so we need to
                     * percolate it up the queue to its correct position */
                    /* Upto log(usedBlocks) iterations */
                    percolate_up(usedBlocks - 1);
                    blocks[block].nodes = next_chunk();
                } else {
                    /* previous block was not dense enough, so push it into the sparse
                     * node cache instead */
                    for (int i = 0; i < (1 << BLOCK_SHIFT); ++i) {
                        if (queue[usedBlocks - 1]->nodes[i].valid()) {
                            set_sparse(
                                block2id(queue[usedBlocks - 1]->block_offset,
                                         i),
                                queue[usedBlocks - 1]->nodes[i]);
                            // invalidate location
                            queue[usedBlocks - 1]->nodes[i] =
                                osmium::Location{};
                        }
                    }
                    /* reuse previous block, as its content is now in the sparse
                     * representation */
                    storedNodes -= queue[usedBlocks - 1]->used();
                    blocks[block].nodes = queue[usedBlocks - 1]->nodes;
                    blocks[queue[usedBlocks - 1]->block_offset].nodes = nullptr;
                    usedBlocks--;
                    cacheUsed -= per_block() * sizeof(osmium::Location);
                }
            } else {
                blocks[block].nodes = next_chunk();
            }

            blocks[block].reset_used();
            blocks[block].block_offset = block;
            if (!blocks[block].nodes) {
                throw std::runtime_error{"Error allocating nodes."};
            }
            queue[usedBlocks] = &blocks[block];
            ++usedBlocks;
            cacheUsed += per_block() * sizeof(osmium::Location);

            /* If we've just used up the last possible block we enter the
             * transition and we change the invariant. To do this we percolate
             * the newly allocated block straight to the head */
            if ((usedBlocks == maxBlocks) || (cacheUsed > cacheSize)) {
                percolate_up(usedBlocks - 1);
            }
        } else {
            if ((allocStrategy & ALLOC_LOSSY) == 0) {
                throw std::runtime_error{
                    "Node cache size is too small to fit all nodes. "
                    "Please increase cache size."};
            }
            /* We've reached the maximum number of blocks, so now we push the
             * current head of the tree down to the right level to restore the
             * priority queue invariant. Upto log(maxBlocks) iterations */

            int i = 0;
            while (2 * i + 1 < usedBlocks - 1) {
                if (queue[2 * i + 1]->used() <= queue[2 * i + 2]->used()) {
                    if (queue[i]->used() > queue[2 * i + 1]->used()) {
                        using std::swap;
                        swap(queue[i], queue[2 * i + 1]);
                        i = 2 * i + 1;
                    } else {
                        break;
                    }
                } else {
                    if (queue[i]->used() > queue[2 * i + 2]->used()) {
                        using std::swap;
                        swap(queue[i], queue[2 * i + 2]);
                        i = 2 * i + 2;
                    } else {
                        break;
                    }
                }
            }
            /* Now the head of the queue is the smallest, so it becomes our
             * replacement candidate */
            blocks[block].nodes = queue[0]->nodes;
            blocks[block].reset_used();
            new (blocks[block].nodes) osmium::Location[per_block()];

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
        if ((usedBlocks < maxBlocks) && (cacheUsed < cacheSize)) {
            expectedpos = usedBlocks - 1;
        } else {
            expectedpos = 0;
        }

        if (queue[expectedpos] != &blocks[block]) {
            if (m_warn_node_order) {
                log_warn("Found out of order node {}"
                         " ({},{}) - this will impact the cache efficiency",
                         id, block, offset);

                // Only warn once
                m_warn_node_order = false;
            }
            return;
        }
    }

    blocks[block].nodes[offset] = location;
    blocks[block].inc_used();
    ++storedNodes;
}

osmium::Location node_ram_cache::get_sparse(osmid_t id) const
{
    int64_t pivotPos = sizeSparseTuples >> 1;
    int64_t minPos = 0;
    int64_t maxPos = sizeSparseTuples;

    while (minPos <= maxPos) {
        if (sparseBlock[pivotPos].id == id) {
            return sparseBlock[pivotPos].coord;
        }
        if ((pivotPos == minPos) || (pivotPos == maxPos)) {
            return osmium::Location{};
        }

        if (sparseBlock[pivotPos].id > id) {
            maxPos = pivotPos;
            pivotPos = minPos + ((maxPos - minPos) >> 1U);
        } else {
            minPos = pivotPos;
            pivotPos = minPos + ((maxPos - minPos) >> 1U);
        }
    }

    return osmium::Location{};
}

osmium::Location node_ram_cache::get_dense(osmid_t id) const
{
    auto const block = id2block(id);

    if (!blocks[block].nodes) {
        return osmium::Location{};
    }

    return blocks[block].nodes[id2offset(id)];
}

node_ram_cache::node_ram_cache(int strategy, int cacheSizeMB)
: allocStrategy(strategy), cacheSize((int64_t)cacheSizeMB * 1024 * 1024)
{
    if (cacheSize == 0) {
        return;
    }

    /* How much we can fit, and make sure it's odd */
    maxBlocks = (cacheSize / (per_block() * sizeof(osmium::Location)));
    maxSparseTuples = (cacheSize / sizeof(ramNodeID)) + 1;

    if ((allocStrategy & ALLOC_DENSE) > 0) {
        log_info("Allocating memory for dense node cache");
        blocks = (ramNodeBlock *)calloc(num_blocks(), sizeof(ramNodeBlock));
        if (!blocks) {
            throw std::runtime_error{
                "Out of memory for node cache dense index, try using "
                "\"--cache-strategy sparse\" instead."};
        }
        queue = (ramNodeBlock **)calloc(maxBlocks, sizeof(ramNodeBlock *));
        /* Use this method of allocation if virtual memory is limited,
         * or if OS allocs physical memory right away, rather than page by page
         * once it is needed.
         */
        if ((allocStrategy & ALLOC_DENSE_CHUNK) > 0) {
            log_info("Allocating dense node cache in block sized chunks");
            if (!queue) {
                throw std::runtime_error{"Out of memory, reduce --cache size."};
            }
        } else {
            log_info("Allocating dense node cache in one big chunk");
            blockCache = (char *)malloc((maxBlocks + 1024) * per_block() *
                                        sizeof(osmium::Location));
            if (!queue || !blockCache) {
                throw std::runtime_error{
                    "Out of memory for dense node cache, reduce --cache size."};
            }
        }
    }

    /* Allocate the full amount of memory given by --cache parameter in one go.
     * If both dense and sparse cache alloc is set, this will allocate up to twice
     * as much virtual memory as specified by --cache. This relies on the OS doing
     * lazy allocation of physical RAM. Extra accounting during setting of nodes
     * is done
     * to ensure physical RAM usage should roughly be no more than --cache
     */

    if ((allocStrategy & ALLOC_SPARSE) > 0) {
        log_info("Allocating memory for sparse node cache");
        if (!blockCache) {
            sparseBlock =
                (ramNodeID *)malloc(maxSparseTuples * sizeof(ramNodeID));
        } else {
            log_info("Sharing dense sparse");
            sparseBlock = (ramNodeID *)blockCache;
        }
        if (!sparseBlock) {
            throw std::runtime_error{
                "Out of memory for sparse node cache, reduce --cache size."};
        }
    }

    log_info("Node-cache: cache={}MB, maxblocks={}*{}, allocation method={}",
             (cacheSize >> 20U), maxBlocks,
             per_block() * sizeof(osmium::Location), allocStrategy);
}

node_ram_cache::~node_ram_cache()
{
    if (cacheSize == 0) {
        return;
    }

    log_info(
        "node cache: stored: {}"
        "({:.2f}%), storage efficiency: {:.2f}% (dense blocks: {}, "
        "sparse nodes: {}), hit rate: {:.2f}%",
        storedNodes, totalNodes == 0 ? 0.0f : 100.0f * storedNodes / totalNodes,
        cacheUsed == 0
            ? 0.0f
            : 100.0f * storedNodes * sizeof(osmium::Location) / cacheUsed,
        usedBlocks, sizeSparseTuples,
        nodesCacheLookups == 0 ? 0.0f
                               : 100.0f * nodesCacheHits / nodesCacheLookups);

    if (((allocStrategy & ALLOC_SPARSE) > 0) && (!blockCache)) {
        free(sparseBlock);
    }

    if ((allocStrategy & ALLOC_DENSE) > 0) {
        if ((allocStrategy & ALLOC_DENSE_CHUNK) > 0) {
            for (int i = 0; i < usedBlocks; ++i) {
                delete[] queue[i]->nodes;
                queue[i]->nodes = nullptr;
            }
        } else {
            free(blockCache);
            blockCache = nullptr;
        }
        free(blocks);
        free(queue);
    }
}

void node_ram_cache::set(osmid_t id, osmium::Location location)
{
    if (cacheSize == 0) {
        return;
    }

    if ((id > 0 && id >> BLOCK_SHIFT >> 32U) ||
        (id < 0 && ~id >> BLOCK_SHIFT >> 32U)) {
        throw std::runtime_error{"Absolute node IDs must not be larger than {}"
                                 " (got {})."_format(1ULL << 42U, id)};
    }
    ++totalNodes;
    /* if ALLOC_DENSE and ALLOC_SPARSE are set, send it through
     * ram_nodes_set_dense. If a block is non dense, it will automatically
     * get pushed to the sparse cache if a block is sparse and ALLOC_SPARSE is set
     */
    if ((allocStrategy & ALLOC_DENSE) != 0) {
        set_dense(id, location);
    } else if ((allocStrategy & ALLOC_SPARSE) != 0) {
        set_sparse(id, location);
    } else {
        // Command line options always have ALLOC_DENSE | ALLOC_SPARSE
        throw std::logic_error{
            "Unexpected cache strategy in node_ram_cache::set with "
            "allocStrategy {}."_format(allocStrategy)};
    }
}

osmium::Location node_ram_cache::get(osmid_t id)
{
    osmium::Location coord;

    if (cacheSize == 0) {
        return coord;
    }

    if (allocStrategy & ALLOC_DENSE) {
        coord = get_dense(id);
    }

    if (allocStrategy & ALLOC_SPARSE && !coord.valid()) {
        coord = get_sparse(id);
    }

    if (coord.valid()) {
        ++nodesCacheHits;
    }
    ++nodesCacheLookups;

    return coord;
}
