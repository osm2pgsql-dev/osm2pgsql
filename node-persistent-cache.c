#define _LARGEFILE64_SOURCE     /* See feature_test_macrors(7) */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <math.h>

#ifdef HAVE_PTHREAD
#include <pthread.h>
#endif


#include "osmtypes.h"
#include "output.h"
#include "node-persistent-cache.h"
#include "node-ram-cache.h"
#include "binarysearcharray.h"

#ifdef __APPLE__
  #define lseek64 lseek
#else
  #ifndef HAVE_LSEEK64
  #if SIZEOF_OFF_T == 8
  #define lseek64 lseek
  #else
  #error Flat nodes cache requires a 64 bit capable seek
  #endif
  #endif
#endif

static const char * node_cache_fname;
static int append_mode;

#ifdef HAVE_PTHREAD
pthread_mutex_t lock_node_persistent_cache = PTHREAD_MUTEX_INITIALIZER;
#endif
struct persistentCacheHeader cacheHeader;
static struct ramNodeBlock writeNodeBlock; /* larger node block for more efficient initial sequential writing of node cache */


static int scale;
static int cache_already_written = 0;


void writeout_dirty_nodes(void * ctx_p, osmid_t id)
{
    struct node_persistent_thread_ctx * thread_ctx = ctx_p;
    int i;

    if (writeNodeBlock.dirty > 0)
    {
        if (lseek64(thread_ctx->node_cache_fd,
                (writeNodeBlock.block_offset << WRITE_NODE_BLOCK_SHIFT)
                        * sizeof(struct ramNode)
                    + sizeof(struct persistentCacheHeader), SEEK_SET) < 0) {
            fprintf(stderr, "Failed to seek to correct position in node cache: %s\n",
                    strerror(errno));
            exit_nicely();
            
        };
        if (write(thread_ctx->node_cache_fd, writeNodeBlock.nodes,
                WRITE_NODE_BLOCK_SIZE * sizeof(struct ramNode))
                < WRITE_NODE_BLOCK_SIZE * sizeof(struct ramNode))
        {
            fprintf(stderr, "Failed to write out node cache: %s\n",
                    strerror(errno));
            exit_nicely();
        }
        cacheHeader.max_initialised_id = ((writeNodeBlock.block_offset + 1)
                << WRITE_NODE_BLOCK_SHIFT) - 1;
        writeNodeBlock.used = 0;
        writeNodeBlock.dirty = 0;
        if (lseek64(thread_ctx->node_cache_fd, 0, SEEK_SET) < 0) {
            fprintf(stderr, "Failed to seek to correct position in node cache: %s\n",
                    strerror(errno));
            exit_nicely();
        };
        if (write(thread_ctx->node_cache_fd, &cacheHeader,
                sizeof(struct persistentCacheHeader))
                != sizeof(struct persistentCacheHeader))
        {
            fprintf(stderr, "Failed to update persistent cache header: %s\n",
                    strerror(errno));
            exit_nicely();
        }
        if (fsync(thread_ctx->node_cache_fd) < 0) {
            fprintf(stderr, "Info: Node cache could not be guaranteed to be made durable. fsync failed: %s\n",
                    strerror(errno));
        };
    }

    if (id < 0) //Indicate that we want to write out any dirty readNode cache as well, used in append mode.
    {
        for (i = 0; i < READ_NODE_CACHE_SIZE; i++)
        {
            if (thread_ctx->readNodeBlockCache[i].dirty)
            {
                if (lseek64(thread_ctx->node_cache_fd,
                        (thread_ctx->readNodeBlockCache[i].block_offset
                                << READ_NODE_BLOCK_SHIFT)
                                * sizeof(struct ramNode)
                                + sizeof(struct persistentCacheHeader),
                            SEEK_SET) < 0) {
                    fprintf(stderr, "Failed to seek to correct position in node cache: %s\n",
                            strerror(errno));
                    exit_nicely();
                };
                if (write(thread_ctx->node_cache_fd, thread_ctx->readNodeBlockCache[i].nodes,
                        READ_NODE_BLOCK_SIZE * sizeof(struct ramNode))
                        < READ_NODE_BLOCK_SIZE * sizeof(struct ramNode))
                {
                    fprintf(stderr, "Failed to write out node cache: %s\n",
                            strerror(errno));
                    exit_nicely();
                }
            }
            thread_ctx->readNodeBlockCache[i].dirty = 0;
        }
    }

}

static void ramNodes_clear(struct ramNode * nodes, int size)
{
    int i;
    for (i = 0; i < size; i++)
    {
#ifdef FIXED_POINT
        nodes[i].lon = INT_MIN;
        nodes[i].lat = INT_MIN;
#else
        nodes[i].lon = NAN;
        nodes[i].lat = NAN;
#endif
    }
}

/**
 * Find the cache block with the lowest usage count for replacement
 */
static int persistent_cache_replace_block(struct node_persistent_thread_ctx * ctx)
{
    int min_used = INT_MAX;
    int block_id = -1;
    int i;

    for (i = 0; i < READ_NODE_CACHE_SIZE; i++)
    {
        if (ctx->readNodeBlockCache[i].used < min_used)
        {
            min_used = ctx->readNodeBlockCache[i].used;
            block_id = i;
        }
    }
    if (min_used > 0)
    {
        for (i = 0; i < READ_NODE_CACHE_SIZE; i++)
        {
            if (ctx->readNodeBlockCache[i].used > 1)
            {
                ctx->readNodeBlockCache[i].used--;
            }
        }
    }
    return block_id;
}

/**
 * Find cache block number by block_offset
 */
static int persistent_cache_find_block(struct node_persistent_thread_ctx * ctx, osmid_t block_offset)
{
    int idx = binary_search_get(ctx->readNodeBlockCacheIdx, block_offset);
    return idx;
}

/**
 * Initialise the persistent cache with NaN values to identify which IDs are valid or not
 */
static void persistent_cache_expand_cache(struct node_persistent_thread_ctx * ctx, osmid_t block_offset)
{
    osmid_t i;
    struct ramNode * dummyNodes = malloc(
            READ_NODE_BLOCK_SIZE * sizeof(struct ramNode));
    if (!dummyNodes) {
        fprintf(stderr, "Out of memory: Could not allocate node structure during cache expansion\n");
        exit_nicely();
    }
    ramNodes_clear(dummyNodes, READ_NODE_BLOCK_SIZE);
    /* Need to expand the persistent node cache */
    if (lseek64(ctx->node_cache_fd,
            cacheHeader.max_initialised_id * sizeof(struct ramNode)
                + sizeof(struct persistentCacheHeader), SEEK_SET) < 0) {
        fprintf(stderr, "Failed to seek to correct position in node cache: %s\n",
                strerror(errno));
        exit_nicely();
    };
    for (i = cacheHeader.max_initialised_id >> READ_NODE_BLOCK_SHIFT;
            i <= block_offset; i++)
    {
        if (write(ctx->node_cache_fd, dummyNodes,
                READ_NODE_BLOCK_SIZE * sizeof(struct ramNode))
                < READ_NODE_BLOCK_SIZE * sizeof(struct ramNode))
        {
            fprintf(stderr, "Failed to expand persistent node cache: %s\n",
                    strerror(errno));
            exit_nicely();
        }
    }
    cacheHeader.max_initialised_id = ((block_offset + 1)
            << READ_NODE_BLOCK_SHIFT) - 1;
    if (lseek64(ctx->node_cache_fd, 0, SEEK_SET) < 0) {
        fprintf(stderr, "Failed to seek to correct position in node cache: %s\n",
                strerror(errno));
        exit_nicely();
    };
    if (write(ctx->node_cache_fd, &cacheHeader, sizeof(struct persistentCacheHeader))
            != sizeof(struct persistentCacheHeader))
    {
        fprintf(stderr, "Failed to update persistent cache header: %s\n",
                strerror(errno));
        exit_nicely();
    }
    free(dummyNodes);
    fsync(ctx->node_cache_fd);
}


static void persistent_cache_nodes_prefetch_async(struct node_persistent_thread_ctx * ctx, osmid_t id)
{
#ifdef HAVE_POSIX_FADVISE
    osmid_t block_offset = id >> READ_NODE_BLOCK_SHIFT;

    osmid_t block_id = persistent_cache_find_block(ctx, block_offset);

    if (block_id < 0)
        {   /* The needed block isn't in cache already, so initiate loading */
        writeout_dirty_nodes(ctx, id);

        /* Make sure the node cache is correctly initialised for the block that will be read */
#ifdef HAVE_PTHREAD
        // As this is a write operation, we need to make sure we synchronize this between all threads
        pthread_mutex_lock(&lock_node_persistent_cache);
#endif
        if (cacheHeader.max_initialised_id
                < ((block_offset + 1) << READ_NODE_BLOCK_SHIFT))
            persistent_cache_expand_cache(ctx,block_offset);
#ifdef HAVE_PTHREAD
    pthread_mutex_unlock(&lock_node_persistent_cache);
#endif

        if (posix_fadvise(ctx->node_cache_fd, (block_offset << READ_NODE_BLOCK_SHIFT) * sizeof(struct ramNode)
                      + sizeof(struct persistentCacheHeader), READ_NODE_BLOCK_SIZE * sizeof(struct ramNode),
                          POSIX_FADV_WILLNEED | POSIX_FADV_RANDOM) != 0) {
            fprintf(stderr, "Info: async prefetch of node cache failed. This might reduce performance\n");
        };
    }
#endif
}


/**
 * Load block offset in a synchronous way.
 */
static int persistent_cache_load_block(struct node_persistent_thread_ctx * ctx, osmid_t block_offset)
{

    int block_id = persistent_cache_replace_block(ctx);

    /* This should only happen if we are in single threaded mode. Otherwise it is likely to lead to thread inconsistencies */
    if (ctx->readNodeBlockCache[block_id].dirty)
    {
        if (lseek64(ctx->node_cache_fd,
                (ctx->readNodeBlockCache[block_id].block_offset
                        << READ_NODE_BLOCK_SHIFT) * sizeof(struct ramNode)
                    + sizeof(struct persistentCacheHeader), SEEK_SET) < 0) {
            fprintf(stderr, "Failed to seek to correct position in node cache: %s\n",
                    strerror(errno));
            exit_nicely();
        };
        if (write(ctx->node_cache_fd, ctx->readNodeBlockCache[block_id].nodes,
                READ_NODE_BLOCK_SIZE * sizeof(struct ramNode))
                < READ_NODE_BLOCK_SIZE * sizeof(struct ramNode))
        {
            fprintf(stderr, "Failed to write out node cache: %s\n",
                    strerror(errno));
            exit_nicely();
        }
        ctx->readNodeBlockCache[block_id].dirty = 0;
    }

    binary_search_remove(ctx->readNodeBlockCacheIdx,
            ctx->readNodeBlockCache[block_id].block_offset);
    ramNodes_clear(ctx->readNodeBlockCache[block_id].nodes, READ_NODE_BLOCK_SIZE);
    ctx->readNodeBlockCache[block_id].block_offset = block_offset;
    ctx->readNodeBlockCache[block_id].used = READ_NODE_CACHE_SIZE; // As we are just loading this, make sure it has a high priority in the LRU

    /* Make sure the node cache is correctly initialised for the block that will be read */
#ifdef HAVE_PTHREAD
    // As this is a write operation, we need to make sure we synchronize this between all threads
    pthread_mutex_lock(&lock_node_persistent_cache);
#endif
    if (cacheHeader.max_initialised_id
            < ((block_offset + 1) << READ_NODE_BLOCK_SHIFT))
    {
        persistent_cache_expand_cache(ctx, block_offset);
    }
#ifdef HAVE_PTHREAD
    pthread_mutex_unlock(&lock_node_persistent_cache);
#endif


    /* Read the block into cache */
    if (lseek64(ctx->node_cache_fd,
            (block_offset << READ_NODE_BLOCK_SHIFT) * sizeof(struct ramNode)
                + sizeof(struct persistentCacheHeader), SEEK_SET) < 0) {
        fprintf(stderr, "Failed to seek to correct position in node cache: %s\n",
                strerror(errno));
        exit_nicely();
    };
    if (read(ctx->node_cache_fd, ctx->readNodeBlockCache[block_id].nodes,
            READ_NODE_BLOCK_SIZE * sizeof(struct ramNode))
            != READ_NODE_BLOCK_SIZE * sizeof(struct ramNode))
    {
        fprintf(stderr, "Failed to read from node cache: %s\n",
                strerror(errno));
        exit(1);
    }
    binary_search_add(ctx->readNodeBlockCacheIdx,
            ctx->readNodeBlockCache[block_id].block_offset, block_id);

    return block_id;
}

static void persisten_cache_nodes_set_create_writeout_block(struct node_persistent_thread_ctx * ctx)
{
    if (write(ctx->node_cache_fd, writeNodeBlock.nodes,
              WRITE_NODE_BLOCK_SIZE * sizeof(struct ramNode))
        < WRITE_NODE_BLOCK_SIZE * sizeof(struct ramNode))
    {
        fprintf(stderr, "Failed to write out node cache: %s\n",
                strerror(errno));
        exit_nicely();
    }
#ifdef HAVE_SYNC_FILE_RANGE
    /* writing out large files can cause trouble on some operating systems.
     * For one, if to much dirty data is in RAM, the whole OS can stall until
     * enough dirty data is written out which can take a while. It can also interfere
     * with outher disk caching operations and might push things out to swap. By forcing the OS to
     * immediately write out the data and blocking after a while, we ensure that no more
     * than a couple of 10s of MB are dirty in RAM at a time.
     * Secondly, the nodes are stored in an additional ram cache during import. Keeping the
     * node cache file in buffer cache therefore duplicates the data wasting 16GB of ram.
     * Therefore tell the OS not to cache the node-persistent-cache during initial import.
     * */
    if (sync_file_range(ctx->node_cache_fd, writeNodeBlock.block_offset*WRITE_NODE_BLOCK_SIZE * sizeof(struct ramNode) +
                        sizeof(struct persistentCacheHeader), WRITE_NODE_BLOCK_SIZE * sizeof(struct ramNode),
                        SYNC_FILE_RANGE_WRITE) < 0) {
        fprintf(stderr, "Info: Sync_file_range writeout has an issue. This shouldn't be anything to worry about.: %s\n",
                strerror(errno));
    };

    if (writeNodeBlock.block_offset > 16) {
        if(sync_file_range(ctx->node_cache_fd, (writeNodeBlock.block_offset - 16)*WRITE_NODE_BLOCK_SIZE * sizeof(struct ramNode) +
                           sizeof(struct persistentCacheHeader), WRITE_NODE_BLOCK_SIZE * sizeof(struct ramNode),
                            SYNC_FILE_RANGE_WAIT_BEFORE | SYNC_FILE_RANGE_WRITE | SYNC_FILE_RANGE_WAIT_AFTER) < 0) {
            fprintf(stderr, "Info: Sync_file_range block has an issue. This shouldn't be anything to worry about.: %s\n",
                strerror(errno));
            
        }
#ifdef HAVE_POSIX_FADVISE
        if (posix_fadvise(ctx->node_cache_fd, (writeNodeBlock.block_offset - 16)*WRITE_NODE_BLOCK_SIZE * sizeof(struct ramNode) +
                          sizeof(struct persistentCacheHeader), WRITE_NODE_BLOCK_SIZE * sizeof(struct ramNode), POSIX_FADV_DONTNEED) !=0 ) {
            fprintf(stderr, "Info: Posix_fadvise failed. This shouldn't be anything to worry about.: %s\n",
                strerror(errno));
        };
#endif
    }
#endif
}

static int persistent_cache_nodes_set_create(struct node_persistent_thread_ctx * ctx, osmid_t id, double lat, double lon)
{
    osmid_t block_offset = id >> WRITE_NODE_BLOCK_SHIFT;
    int i;

    if (cache_already_written)
        return 0;

    if (writeNodeBlock.block_offset != block_offset)
    {
        if (writeNodeBlock.dirty)
        {
            persisten_cache_nodes_set_create_writeout_block(ctx);
            writeNodeBlock.used = 0;
            writeNodeBlock.dirty = 0;
            /* After writing out the node block, the file pointer is at the next block level */
            writeNodeBlock.block_offset++;
            cacheHeader.max_initialised_id = (writeNodeBlock.block_offset
                    << WRITE_NODE_BLOCK_SHIFT) - 1;
        }
        if (writeNodeBlock.block_offset > block_offset)
        {
            fprintf(stderr,
                    "ERROR: Block_offset not in sequential order: %" PRIdOSMID "%" PRIdOSMID "\n",
                    writeNodeBlock.block_offset, block_offset);
            exit_nicely();
        }

        /* We need to fill the intermediate node cache with node nodes to identify which nodes are valid */
        for (i = writeNodeBlock.block_offset; i < block_offset; i++)
        {
            ramNodes_clear(writeNodeBlock.nodes, WRITE_NODE_BLOCK_SIZE);
            persisten_cache_nodes_set_create_writeout_block(ctx);
        }

        ramNodes_clear(writeNodeBlock.nodes, WRITE_NODE_BLOCK_SIZE);
        writeNodeBlock.used = 0;
        writeNodeBlock.block_offset = block_offset;
    }
#ifdef FIXED_POINT
    writeNodeBlock.nodes[id & WRITE_NODE_BLOCK_MASK].lat = DOUBLE_TO_FIX(lat);
    writeNodeBlock.nodes[id & WRITE_NODE_BLOCK_MASK].lon = DOUBLE_TO_FIX(lon);
#else
    writeNodeBlock.nodes[id & WRITE_NODE_BLOCK_MASK].lat = lat;
    writeNodeBlock.nodes[id & WRITE_NODE_BLOCK_MASK].lon = lon;
#endif
    writeNodeBlock.used++;
    writeNodeBlock.dirty = 1;

    return 0;
}

static int persistent_cache_nodes_set_append(struct node_persistent_thread_ctx * ctx, osmid_t id, double lat, double lon)
{
    osmid_t block_offset = id >> READ_NODE_BLOCK_SHIFT;

    int block_id = persistent_cache_find_block(ctx, block_offset);

    if (block_id < 0)
        block_id = persistent_cache_load_block(ctx, block_offset);

#ifdef FIXED_POINT
    if (isnan(lat) && isnan(lon))
    {
        ctx->readNodeBlockCache[block_id].nodes[id & READ_NODE_BLOCK_MASK].lat =
                INT_MIN;
        ctx->readNodeBlockCache[block_id].nodes[id & READ_NODE_BLOCK_MASK].lon =
                INT_MIN;
    }
    else
    {
        ctx->readNodeBlockCache[block_id].nodes[id & READ_NODE_BLOCK_MASK].lat =
                DOUBLE_TO_FIX(lat);
        ctx->readNodeBlockCache[block_id].nodes[id & READ_NODE_BLOCK_MASK].lon =
                DOUBLE_TO_FIX(lon);
    }
#else
    readNodeBlockCache[block_id].nodes[id & READ_NODE_BLOCK_MASK].lat = lat;
    readNodeBlockCache[block_id].nodes[id & READ_NODE_BLOCK_MASK].lon = lon;
#endif
    ctx->readNodeBlockCache[block_id].used++;
    ctx->readNodeBlockCache[block_id].dirty = 1;

    return 1;
}

int persistent_cache_nodes_set(void * ctx_p, osmid_t id, double lat, double lon)
{
    struct node_persistent_thread_ctx * ctx = ctx_p;
    return append_mode ?
            persistent_cache_nodes_set_append(ctx, id, lat, lon) :
            persistent_cache_nodes_set_create(ctx, id, lat, lon);
}

int persistent_cache_nodes_get(void * ctx_p, struct osmNode *out, osmid_t id)
{
    struct node_persistent_thread_ctx * ctx = ctx_p;
    osmid_t block_offset = id >> READ_NODE_BLOCK_SHIFT;

    osmid_t block_id = persistent_cache_find_block(ctx, block_offset);

    if (block_id < 0)
    {
        writeout_dirty_nodes(ctx, id);
        block_id = persistent_cache_load_block(ctx, block_offset);
    }

    ctx->readNodeBlockCache[block_id].used++;

#ifdef FIXED_POINT
    if ((ctx->readNodeBlockCache[block_id].nodes[id & READ_NODE_BLOCK_MASK].lat
            == INT_MIN)
            && (ctx->readNodeBlockCache[block_id].nodes[id & READ_NODE_BLOCK_MASK].lon
                    == INT_MIN))
    {
        return 1;
    }
    else
    {
        out->lat =
                FIX_TO_DOUBLE(ctx->readNodeBlockCache[block_id].nodes[id & READ_NODE_BLOCK_MASK].lat);
        out->lon =
                FIX_TO_DOUBLE(ctx->readNodeBlockCache[block_id].nodes[id & READ_NODE_BLOCK_MASK].lon);
        return 0;
    }
#else
    if ((isnan(readNodeBlockCache[block_id].nodes[id & READ_NODE_BLOCK_MASK].lat)) &&
            (isnan(readNodeBlockCache[block_id].nodes[id & READ_NODE_BLOCK_MASK].lon)))
    {
        return 1;
    }
    else
    {
        out->lat = readNodeBlockCache[block_id].nodes[id & READ_NODE_BLOCK_MASK].lat;
        out->lon = readNodeBlockCache[block_id].nodes[id & READ_NODE_BLOCK_MASK].lon;
        return 0;
    }
#endif

    return 0;
}

int persistent_cache_nodes_get_list(void * ctx_p, struct osmNode *nodes, osmid_t *ndids,
        int nd_count)
{
    struct node_persistent_thread_ctx * ctx = ctx_p;
    int count = 0;
    int i;
    for (i = 0; i < nd_count; i++)
    {
        /* Check cache first */
        if (ram_cache_nodes_get(&nodes[i], ndids[i]) == 0)
        {
            count++;
        }
        else
        {
            nodes[i].lat = NAN;
            nodes[i].lon = NAN;
        }
    }
    if (count == nd_count)
        return count;

    for (i = 0; i < nd_count; i++)
    {
        /* In order to have a higher OS level I/O queue depth
           issue posix_fadvise(WILLNEED) requests for all I/O */
        if (isnan(nodes[i].lat) && isnan(nodes[i].lon))
            persistent_cache_nodes_prefetch_async(ctx, ndids[i]);
    }
    for (i = 0; i < nd_count; i++)
    {
        if ((isnan(nodes[i].lat) && isnan(nodes[i].lon))
                && (persistent_cache_nodes_get(ctx, &(nodes[i]), ndids[i]) == 0))
            count++;
    }

    if (count < nd_count)
    {
        int j = 0;
        for (i = 0; i < nd_count; i++)
        {
            if (!isnan(nodes[i].lat))
            {
                nodes[j].lat = nodes[i].lat;
                nodes[j].lon = nodes[i].lon;
                j++;
            }
        }
        for (i = count; i < nd_count; i++)
        {
            nodes[i].lat = NAN;
            nodes[i].lon = NAN;
        }
    }

    return count;
}

void * init_node_persistent_cache(const struct output_options *options, int append) {
    struct node_persistent_thread_ctx * ctx = malloc(sizeof(struct node_persistent_thread_ctx));
    int i, err;
    scale = options->scale;
    append_mode = append;
    node_cache_fname = options->flat_node_file;
    fprintf(stderr, "Mid: loading persistent node cache from %s\n",
            node_cache_fname);

    /* To be thread safe in reading from the node_persistent cache
     * we create a per thread context for all read operations. This contains
     * the file descriptor to ensure we can seek in the node file independently
     * in each thread. We also keep separate read caches per thread to not have to
     * synchronize between threads. As we don't synchronize between threads, we
     * need to make sure that all write operations have happened before
     * the threading stage.
     *
     */
    ctx->readNodeBlockCacheIdx = init_search_array(READ_NODE_CACHE_SIZE);
    
    /* Setup the file for the node position cache */
    if (append_mode)
    {
        ctx->node_cache_fd = open(node_cache_fname, O_RDWR, S_IRUSR | S_IWUSR);
        if (ctx->node_cache_fd < 0)
        {
            fprintf(stderr, "Failed to open node cache file: %s\n",
                    strerror(errno));
            exit_nicely();
        }
    }
    else
    {
#ifdef HAVE_PTHREAD
        pthread_mutex_lock(&lock_node_persistent_cache);
#endif
        if (cache_already_written)
        {
            ctx->node_cache_fd = open(node_cache_fname, O_RDWR, S_IRUSR | S_IWUSR);
        }
        else
        {
            ctx->node_cache_fd = open(node_cache_fname, O_RDWR | O_CREAT | O_TRUNC,
                    S_IRUSR | S_IWUSR);
        }

        if (ctx->node_cache_fd < 0)
        {
            fprintf(stderr, "Failed to create node cache file: %s\n",
                    strerror(errno));
            exit_nicely();
        }
        if (lseek64(ctx->node_cache_fd, 0, SEEK_SET) < 0) {
            fprintf(stderr, "Failed to seek to correct position in node cache: %s\n",
                    strerror(errno));
            exit_nicely();
        };
        if (cache_already_written == 0)
        {
            #ifdef HAVE_POSIX_FALLOCATE
            if ((err = posix_fallocate(ctx->node_cache_fd, 0,
                    sizeof(struct ramNode) * MAXIMUM_INITIAL_ID)) != 0)
            {
                if (err == ENOSPC) {
                    fprintf(stderr, "Failed to allocate space for node cache file: No space on disk\n");
                } else if (err == EFBIG) {
                    fprintf(stderr, "Failed to allocate space for node cache file: File is too big\n");
                } else {
                    fprintf(stderr, "Failed to allocate space for node cache file: Internal error %i\n", err);
                }
                close(ctx->node_cache_fd);
                exit_nicely();
            }
            fprintf(stderr, "Allocated space for persistent node cache file\n");
            #endif
            writeNodeBlock.nodes = malloc(
                    WRITE_NODE_BLOCK_SIZE * sizeof(struct ramNode));
            if (!writeNodeBlock.nodes) {
                fprintf(stderr, "Out of memory: Failed to allocate node writeout buffer\n");
                exit_nicely();
            }
            ramNodes_clear(writeNodeBlock.nodes, WRITE_NODE_BLOCK_SIZE);
            writeNodeBlock.block_offset = 0;
            writeNodeBlock.used = 0;
            writeNodeBlock.dirty = 0;
            cacheHeader.format_version = PERSISTENT_CACHE_FORMAT_VERSION;
            cacheHeader.id_size = sizeof(osmid_t);
            cacheHeader.max_initialised_id = 0;
            if (lseek64(ctx->node_cache_fd, 0, SEEK_SET) < 0) {
                fprintf(stderr, "Failed to seek to correct position in node cache: %s\n",
                        strerror(errno));
                exit_nicely();
            };
            if (write(ctx->node_cache_fd, &cacheHeader,
                    sizeof(struct persistentCacheHeader))
                    != sizeof(struct persistentCacheHeader))
            {
                fprintf(stderr, "Failed to write persistent cache header: %s\n",
                        strerror(errno));
                exit_nicely();
            }
        }
#ifdef HAVE_PTHREAD
        pthread_mutex_unlock(&lock_node_persistent_cache);
#endif


    }
    if (lseek64(ctx->node_cache_fd, 0, SEEK_SET) < 0) {
        fprintf(stderr, "Failed to seek to correct position in node cache: %s\n",
                strerror(errno));
        exit_nicely();
    };
    if (read(ctx->node_cache_fd, &cacheHeader, sizeof(struct persistentCacheHeader))
            != sizeof(struct persistentCacheHeader))
    {
        fprintf(stderr, "Failed to read persistent cache header: %s\n",
                strerror(errno));
        exit_nicely();
    }
    if (cacheHeader.format_version != PERSISTENT_CACHE_FORMAT_VERSION)
    {
        fprintf(stderr, "Persistent cache header is wrong version\n");
        exit_nicely();
    }

    if (cacheHeader.id_size != sizeof(osmid_t))
    {
        fprintf(stderr, "Persistent cache header is wrong id type\n");
        exit_nicely();
    }

    fprintf(stderr,"Maximum node in persistent node cache: %" PRIdOSMID "\n", cacheHeader.max_initialised_id);

    ctx->readNodeBlockCache = malloc(
            READ_NODE_CACHE_SIZE * sizeof(struct ramNodeBlock));
    if (!ctx->readNodeBlockCache) {
        fprintf(stderr, "Out of memory: Failed to allocate node read cache\n");
        exit_nicely();
    }
    for (i = 0; i < READ_NODE_CACHE_SIZE; i++)
    {
        ctx->readNodeBlockCache[i].nodes = malloc(
                READ_NODE_BLOCK_SIZE * sizeof(struct ramNode));
        if (!ctx->readNodeBlockCache[i].nodes) {
            fprintf(stderr, "Out of memory: Failed to allocate node read cache\n");
            exit_nicely();
        }
        ctx->readNodeBlockCache[i].block_offset = -1;
        ctx->readNodeBlockCache[i].used = 0;
        ctx->readNodeBlockCache[i].dirty = 0;
    }
    return ctx;
}

void shutdown_node_persistent_cache(void * ctx_p)
{
    struct node_persistent_thread_ctx * ctx = ctx_p;
    int i;
    writeout_dirty_nodes(ctx, -1);

#ifdef HAVE_PTHREAD
    pthread_mutex_lock(&lock_node_persistent_cache);
#endif
    if (lseek64(ctx->node_cache_fd, 0, SEEK_SET) < 0) {
        fprintf(stderr, "Failed to seek to correct position in node cache: %s\n",
                strerror(errno));
        exit_nicely();
    };
    if (write(ctx->node_cache_fd, &cacheHeader, sizeof(struct persistentCacheHeader))
            != sizeof(struct persistentCacheHeader))
    {
        fprintf(stderr, "Failed to update persistent cache header: %s\n",
                strerror(errno));
        exit_nicely();
    }
    fprintf(stderr,"Maximum node in persistent node cache: %" PRIdOSMID "\n", cacheHeader.max_initialised_id);

    fsync(ctx->node_cache_fd);
#ifdef HAVE_PTHREAD
    pthread_mutex_unlock(&lock_node_persistent_cache);
#endif

    if (close(ctx->node_cache_fd) != 0)
    {
        fprintf(stderr, "Failed to close node cache file: %s\n",
                strerror(errno));
    }

    for (i = 0; i < READ_NODE_CACHE_SIZE; i++)
    {
        free(ctx->readNodeBlockCache[i].nodes);
    }
    shutdown_search_array(&ctx->readNodeBlockCacheIdx);
    free(ctx->readNodeBlockCache);
    ctx->readNodeBlockCache = NULL;
    free(ctx);
}
