#define _LARGEFILE64_SOURCE     /* See feature_test_macrors(7) */

#include "config.h"

#include <algorithm>
#include <stdexcept>

#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "node-persistent-cache.hpp"
#include "options.hpp"
#include "osmtypes.hpp"
#include "output.hpp"
#include "util.hpp"

#ifdef _WIN32
 #include "win_fsync.h"
 #define lseek64 _lseeki64
 #ifndef S_IRUSR
  #define S_IRUSR S_IREAD
 #endif
 #ifndef S_IWUSR
  #define S_IWUSR S_IWRITE
 #endif
#else
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
#endif

void node_persistent_cache::writeout_dirty_nodes()
{
    for (int i = 0; i < READ_NODE_CACHE_SIZE; i++)
    {
        if (readNodeBlockCache[i].dirty())
        {
            if (lseek64(node_cache_fd,
                    ((osmid_t) readNodeBlockCache[i].block_offset
                            << READ_NODE_BLOCK_SHIFT)
                            * sizeof(ramNode)
                            + sizeof(persistentCacheHeader),
                        SEEK_SET) < 0) {
                fprintf(stderr, "Failed to seek to correct position in node cache: %s\n",
                        strerror(errno));
                util::exit_nicely();
            };
            if (write(node_cache_fd, readNodeBlockCache[i].nodes,
                    READ_NODE_BLOCK_SIZE * sizeof(ramNode))
                    < ssize_t(READ_NODE_BLOCK_SIZE * sizeof(ramNode)))
            {
                fprintf(stderr, "Failed to write out node cache: %s\n",
                        strerror(errno));
                util::exit_nicely();
            }
        }
        readNodeBlockCache[i].reset_used();
    }
}


/**
 * Find the cache block with the lowest usage count for replacement
 */
size_t node_persistent_cache::replace_block()
{
    int min_used = INT_MAX;
    int block_id = -1;

    for (int i = 0; i < READ_NODE_CACHE_SIZE; i++)
    {
        if (readNodeBlockCache[i].used() < min_used)
        {
            min_used = readNodeBlockCache[i].used();
            block_id = i;
        }
    }
    if (min_used > 0)
    {
        for (int i = 0; i < READ_NODE_CACHE_SIZE; i++)
        {
            if (readNodeBlockCache[i].used() > 1)
            {
                readNodeBlockCache[i].dec_used();
            }
        }
    }
    return block_id;
}

/**
 * Find cache block number by block_offset
 */
int node_persistent_cache::find_block(osmid_t block_offset)
{
    cache_index::iterator it = std::lower_bound(readNodeBlockCacheIdx.begin(),
                                                readNodeBlockCacheIdx.end(),
                                                block_offset);
    if (it != readNodeBlockCacheIdx.end() && it->key == block_offset)
        return it->value;

    return -1;
}

void node_persistent_cache::remove_from_cache_idx(osmid_t block_offset)
{
    cache_index::iterator it = std::lower_bound(readNodeBlockCacheIdx.begin(),
                                                readNodeBlockCacheIdx.end(),
                                                block_offset);

    if (it == readNodeBlockCacheIdx.end() || it->key != block_offset)
        return;

    readNodeBlockCacheIdx.erase(it);
}

void node_persistent_cache::add_to_cache_idx(cache_index_entry const &entry)
{
    cache_index::iterator it = std::lower_bound(readNodeBlockCacheIdx.begin(),
                                                readNodeBlockCacheIdx.end(),
                                                entry);
    readNodeBlockCacheIdx.insert(it, entry);
}

// A cache block with invalid nodes, just for writing out empty cache blocks
static const ramNode nullNodes[READ_NODE_BLOCK_SIZE];
/**
 * Initialise the persistent cache with NaN values to identify which IDs are valid or not
 */
void node_persistent_cache::expand_cache(osmid_t block_offset)
{
    /* Need to expand the persistent node cache */
    if (lseek64(node_cache_fd,
            cacheHeader.max_initialised_id * sizeof(ramNode)
                + sizeof(persistentCacheHeader), SEEK_SET) < 0) {
        fprintf(stderr, "Failed to seek to correct position in node cache: %s\n",
                strerror(errno));
        util::exit_nicely();
    };
    for (osmid_t i = cacheHeader.max_initialised_id >> READ_NODE_BLOCK_SHIFT;
            i <= block_offset; i++)
    {
        if (write(node_cache_fd, nullNodes, sizeof(nullNodes))
                < ssize_t(sizeof(nullNodes)))
        {
            fprintf(stderr, "Failed to expand persistent node cache: %s\n",
                    strerror(errno));
            util::exit_nicely();
        }
    }
    cacheHeader.max_initialised_id = ((block_offset + 1)
            << READ_NODE_BLOCK_SHIFT) - 1;
    if (lseek64(node_cache_fd, 0, SEEK_SET) < 0) {
        fprintf(stderr, "Failed to seek to correct position in node cache: %s\n",
                strerror(errno));
        util::exit_nicely();
    };
    if (write(node_cache_fd, &cacheHeader, sizeof(struct persistentCacheHeader))
            != ssize_t(sizeof(struct persistentCacheHeader)))
    {
        fprintf(stderr, "Failed to update persistent cache header: %s\n",
                strerror(errno));
        util::exit_nicely();
    }
    fsync(node_cache_fd);
}


void node_persistent_cache::nodes_prefetch_async(osmid_t id)
{
#ifdef HAVE_POSIX_FADVISE
    osmid_t block_offset = id >> READ_NODE_BLOCK_SHIFT;

    const int block_id = find_block(block_offset);

    if (block_id < 0) {
        // The needed block isn't in cache already, so initiate loading
        if (cacheHeader.max_initialised_id < id) {
            fprintf(stderr, "Warning: reading node outside node cache. (%lu vs. %lu)\n",
                    cacheHeader.max_initialised_id, id);
            return;
        }

        if (posix_fadvise(node_cache_fd, (block_offset << READ_NODE_BLOCK_SHIFT) * sizeof(ramNode)
                      + sizeof(persistentCacheHeader), READ_NODE_BLOCK_SIZE * sizeof(ramNode),
                          POSIX_FADV_WILLNEED | POSIX_FADV_RANDOM) != 0) {
            fprintf(stderr, "Info: async prefetch of node cache failed. This might reduce performance\n");
        };
    }
#endif
}


/**
 * Load block offset in a synchronous way.
 */
int node_persistent_cache::load_block(osmid_t block_offset)
{
    const size_t block_id = replace_block();

    if (readNodeBlockCache[block_id].dirty())
    {
        if (lseek64(node_cache_fd,
                ((osmid_t) readNodeBlockCache[block_id].block_offset
                        << READ_NODE_BLOCK_SHIFT) * sizeof(ramNode)
                    + sizeof(struct persistentCacheHeader), SEEK_SET) < 0) {
            fprintf(stderr, "Failed to seek to correct position in node cache: %s\n",
                    strerror(errno));
            util::exit_nicely();
        };
        if (write(node_cache_fd, readNodeBlockCache[block_id].nodes,
                READ_NODE_BLOCK_SIZE * sizeof(ramNode))
                < ssize_t(READ_NODE_BLOCK_SIZE * sizeof(ramNode)))
        {
            fprintf(stderr, "Failed to write out node cache: %s\n",
                    strerror(errno));
            util::exit_nicely();
        }
        readNodeBlockCache[block_id].reset_used();
    }

    if (readNodeBlockCache[block_id].nodes) {
        remove_from_cache_idx((osmid_t) readNodeBlockCache[block_id].block_offset);
        new(readNodeBlockCache[block_id].nodes) ramNode[READ_NODE_BLOCK_SIZE];
    } else {
        readNodeBlockCache[block_id].nodes = new ramNode[READ_NODE_BLOCK_SIZE];
        if (!readNodeBlockCache[block_id].nodes) {
            fprintf(stderr, "Out of memory: Failed to allocate node read cache\n");
            util::exit_nicely();
        }
    }
    readNodeBlockCache[block_id].block_offset = block_offset;
    readNodeBlockCache[block_id].set_used(READ_NODE_CACHE_SIZE);

    /* Make sure the node cache is correctly initialised for the block that will be read */
    if (cacheHeader.max_initialised_id
            < ((block_offset + 1) << READ_NODE_BLOCK_SHIFT))
    {
        expand_cache(block_offset);
    }

    /* Read the block into cache */
    if (lseek64(node_cache_fd,
            (block_offset << READ_NODE_BLOCK_SHIFT) * sizeof(ramNode)
                + sizeof(struct persistentCacheHeader), SEEK_SET) < 0) {
        fprintf(stderr, "Failed to seek to correct position in node cache: %s\n",
                strerror(errno));
        util::exit_nicely();
    };
    if (read(node_cache_fd, readNodeBlockCache[block_id].nodes,
            READ_NODE_BLOCK_SIZE * sizeof(ramNode))
            != READ_NODE_BLOCK_SIZE * sizeof(ramNode))
    {
        fprintf(stderr, "Failed to read from node cache: %s\n",
                strerror(errno));
        exit(1);
    }
    add_to_cache_idx(cache_index_entry(block_offset, block_id));

    return block_id;
}

void node_persistent_cache::nodes_set_create_writeout_block()
{
    if (write(node_cache_fd, writeNodeBlock.nodes,
              WRITE_NODE_BLOCK_SIZE * sizeof(ramNode))
        < ssize_t(WRITE_NODE_BLOCK_SIZE * sizeof(ramNode)))
    {
        fprintf(stderr, "Failed to write out node cache: %s\n",
                strerror(errno));
        util::exit_nicely();
    }
#ifdef HAVE_SYNC_FILE_RANGE
    /* writing out large files can cause trouble on some operating systems.
     * For one, if to much dirty data is in RAM, the whole OS can stall until
     * enough dirty data is written out which can take a while. It can also interfere
     * with other disk caching operations and might push things out to swap. By forcing the OS to
     * immediately write out the data and blocking after a while, we ensure that no more
     * than a couple of 10s of MB are dirty in RAM at a time.
     * Secondly, the nodes are stored in an additional ram cache during import. Keeping the
     * node cache file in buffer cache therefore duplicates the data wasting 16GB of ram.
     * Therefore tell the OS not to cache the node-persistent-cache during initial import.
     * */
    if (sync_file_range(node_cache_fd, (osmid_t) writeNodeBlock.block_offset*WRITE_NODE_BLOCK_SIZE * sizeof(ramNode) +
                        sizeof(persistentCacheHeader), WRITE_NODE_BLOCK_SIZE * sizeof(ramNode),
                        SYNC_FILE_RANGE_WRITE) < 0) {
        fprintf(stderr, "Info: Sync_file_range writeout has an issue. This shouldn't be anything to worry about.: %s\n",
                strerror(errno));
    };

    if (writeNodeBlock.block_offset > 16) {
        if(sync_file_range(node_cache_fd, ((osmid_t) writeNodeBlock.block_offset - 16)*WRITE_NODE_BLOCK_SIZE * sizeof(ramNode) +
                           sizeof(persistentCacheHeader), WRITE_NODE_BLOCK_SIZE * sizeof(ramNode),
                            SYNC_FILE_RANGE_WAIT_BEFORE | SYNC_FILE_RANGE_WRITE | SYNC_FILE_RANGE_WAIT_AFTER) < 0) {
            fprintf(stderr, "Info: Sync_file_range block has an issue. This shouldn't be anything to worry about.: %s\n",
                strerror(errno));

        }
#ifdef HAVE_POSIX_FADVISE
        if (posix_fadvise(node_cache_fd, ((osmid_t) writeNodeBlock.block_offset - 16)*WRITE_NODE_BLOCK_SIZE * sizeof(ramNode) +
                          sizeof(persistentCacheHeader), WRITE_NODE_BLOCK_SIZE * sizeof(ramNode), POSIX_FADV_DONTNEED) !=0 ) {
            fprintf(stderr, "Info: Posix_fadvise failed. This shouldn't be anything to worry about.: %s\n",
                strerror(errno));
        };
#endif
    }
#endif
}

void node_persistent_cache::set_create(osmid_t id, double lat, double lon)
{
    assert(!append_mode);
    assert(!read_mode);

    int32_t block_offset = id >> WRITE_NODE_BLOCK_SHIFT;

    if (writeNodeBlock.block_offset != block_offset)
    {
        if (writeNodeBlock.dirty())
        {
            nodes_set_create_writeout_block();
            /* After writing out the node block, the file pointer is at the next block level */
            writeNodeBlock.block_offset++;
            cacheHeader.max_initialised_id = ((osmid_t) writeNodeBlock.block_offset
                    << WRITE_NODE_BLOCK_SHIFT) - 1;
        }
        if (writeNodeBlock.block_offset > block_offset)
        {
            fprintf(stderr,
                    "ERROR: Block_offset not in sequential order: %d %d\n",
                    writeNodeBlock.block_offset, block_offset);
            util::exit_nicely();
        }

        new(writeNodeBlock.nodes) ramNode[WRITE_NODE_BLOCK_SIZE];

        /* We need to fill the intermediate node cache with node nodes to identify which nodes are valid */
        while (writeNodeBlock.block_offset < block_offset)
        {
            nodes_set_create_writeout_block();
            writeNodeBlock.block_offset++;
        }

    }

    writeNodeBlock.nodes[id & WRITE_NODE_BLOCK_MASK] = ramNode(lon, lat);
    writeNodeBlock.set_dirty();
}

void node_persistent_cache::set_append(osmid_t id, double lat, double lon)
{
    assert(!read_mode);

    osmid_t block_offset = id >> READ_NODE_BLOCK_SHIFT;

    int block_id = find_block(block_offset);

    if (block_id < 0)
        block_id = load_block(block_offset);

    if (std::isnan(lat) && std::isnan(lon)) {
        readNodeBlockCache[block_id].nodes[id & READ_NODE_BLOCK_MASK] = ramNode();
    } else {
        readNodeBlockCache[block_id].nodes[id & READ_NODE_BLOCK_MASK] = ramNode(lon, lat);
    }
    readNodeBlockCache[block_id].inc_used();
    readNodeBlockCache[block_id].set_dirty();
}

void node_persistent_cache::set(osmid_t id, double lat, double lon)
{
    if (append_mode) {
        set_append(id, lat, lon);
    } else {
        set_create(id, lat, lon);
    }
}

int node_persistent_cache::get(osmNode *out, osmid_t id)
{
    set_read_mode();

    osmid_t block_offset = id >> READ_NODE_BLOCK_SHIFT;

    int block_id = find_block(block_offset);

    if (block_id < 0)
    {
        block_id = load_block(block_offset);
    }

    readNodeBlockCache[block_id].inc_used();

    if (!readNodeBlockCache[block_id].nodes[id & READ_NODE_BLOCK_MASK].is_valid())
        return 1;

    out->lat = readNodeBlockCache[block_id].nodes[id & READ_NODE_BLOCK_MASK].lat();
    out->lon = readNodeBlockCache[block_id].nodes[id & READ_NODE_BLOCK_MASK].lon();

    return 0;
}

size_t node_persistent_cache::get_list(nodelist_t &out, osmium::WayNodeList const &nds)
{
    set_read_mode();

    out.assign(nds.size(), osmNode());

    bool need_fetch = false;
    for (size_t i = 0; i < nds.size(); ++i) {
        /* Check cache first */
        if (ram_cache->get(&out[i], nds[i].ref()) != 0) {
            /* In order to have a higher OS level I/O queue depth
               issue posix_fadvise(WILLNEED) requests for all I/O */
            nodes_prefetch_async(nds[i].ref());
            need_fetch = true;
        }
    }
    if (!need_fetch)
        return out.size();

    size_t wrtidx = 0;
    for (size_t i = 0; i < nds.size(); i++) {
        if (std::isnan(out[i].lat) && std::isnan(out[i].lon)) {
            if (get(&(out[wrtidx]), nds[i].ref()) == 0)
                wrtidx++;
        } else {
            if (wrtidx < i)
                out[wrtidx] = out[i];
            wrtidx++;
        }
    }

    out.resize(wrtidx);

    return wrtidx;
}

void node_persistent_cache::set_read_mode()
{
    if (read_mode)
        return;

    if (writeNodeBlock.dirty()) {
        assert(!append_mode);
        nodes_set_create_writeout_block();
        writeNodeBlock.reset_used();
        writeNodeBlock.block_offset++;
        cacheHeader.max_initialised_id = ((osmid_t) writeNodeBlock.block_offset
                << WRITE_NODE_BLOCK_SHIFT) - 1;

        /* write out the header */
        if (lseek64(node_cache_fd, 0, SEEK_SET) < 0) {
            fprintf(stderr, "Failed to seek to correct position in node cache: %s\n",
                    strerror(errno));
            util::exit_nicely();
        };
        if (write(node_cache_fd, &cacheHeader, sizeof(persistentCacheHeader))
                != sizeof(persistentCacheHeader)) {
            fprintf(stderr, "Failed to update persistent cache header: %s\n",
                    strerror(errno));
            util::exit_nicely();
        }
    }

    read_mode = true;
}

node_persistent_cache::node_persistent_cache(const options_t *options, bool append,
                                             bool ro, std::shared_ptr<node_ram_cache> ptr)
    : node_cache_fd(0), node_cache_fname(nullptr), append_mode(append), cacheHeader(),
      writeNodeBlock(), readNodeBlockCache(nullptr), read_mode(ro), ram_cache(ptr)
{
    if (options->flat_node_file) {
        node_cache_fname = options->flat_node_file->c_str();
    } else {
        throw std::runtime_error("Unable to set up persistent cache: the name "
                                 "of the flat node file was not set.");
    }
    fprintf(stderr, "Mid: loading persistent node cache from %s\n",
            node_cache_fname);

    readNodeBlockCacheIdx.reserve(READ_NODE_CACHE_SIZE);

    /* Setup the file for the node position cache */
    if (append_mode)
    {
        node_cache_fd = open(node_cache_fname, O_RDWR, S_IRUSR | S_IWUSR);
        if (node_cache_fd < 0)
        {
            fprintf(stderr, "Failed to open node cache file: %s\n",
                    strerror(errno));
            util::exit_nicely();
        }
    }
    else
    {
        if (read_mode)
        {
            node_cache_fd = open(node_cache_fname, O_RDWR, S_IRUSR | S_IWUSR);
        }
        else
        {
            node_cache_fd = open(node_cache_fname, O_RDWR | O_CREAT | O_TRUNC,
                    S_IRUSR | S_IWUSR);
        }

        if (node_cache_fd < 0)
        {
            fprintf(stderr, "Failed to create node cache file: %s\n",
                    strerror(errno));
            util::exit_nicely();
        }
        if (lseek64(node_cache_fd, 0, SEEK_SET) < 0) {
            fprintf(stderr, "Failed to seek to correct position in node cache: %s\n",
                    strerror(errno));
            util::exit_nicely();
        };

        writeNodeBlock.block_offset = 0;

        if (!read_mode)
        {
            #ifdef HAVE_POSIX_FALLOCATE
            int err;
            if ((err = posix_fallocate(node_cache_fd, 0,
                    sizeof(ramNode) * MAXIMUM_INITIAL_ID)) != 0) {
                if (err == ENOSPC) {
                    fprintf(stderr, "Failed to allocate space for node cache file: No space on disk\n");
                } else if (err == EFBIG) {
                    fprintf(stderr, "Failed to allocate space for node cache file: File is too big\n");
                } else {
                    fprintf(stderr, "Failed to allocate space for node cache file: Internal error %i\n", err);
                }

                close(node_cache_fd);
                util::exit_nicely();
            }
            fprintf(stderr, "Allocated space for persistent node cache file\n");
            #endif

            writeNodeBlock.nodes = new ramNode[WRITE_NODE_BLOCK_SIZE];
            if (!writeNodeBlock.nodes) {
                fprintf(stderr, "Out of memory: Failed to allocate node writeout buffer\n");
                util::exit_nicely();
            }
            cacheHeader.format_version = PERSISTENT_CACHE_FORMAT_VERSION;
            cacheHeader.id_size = sizeof(osmid_t);
            cacheHeader.max_initialised_id = 0;
            if (lseek64(node_cache_fd, 0, SEEK_SET) < 0) {
                fprintf(stderr, "Failed to seek to correct position in node cache: %s\n",
                        strerror(errno));
                util::exit_nicely();
            };
            if (write(node_cache_fd, &cacheHeader,
                    sizeof(struct persistentCacheHeader))
                    != sizeof(struct persistentCacheHeader))
            {
                fprintf(stderr, "Failed to write persistent cache header: %s\n",
                        strerror(errno));
                util::exit_nicely();
            }
        }

    }
    if (lseek64(node_cache_fd, 0, SEEK_SET) < 0) {
        fprintf(stderr, "Failed to seek to correct position in node cache: %s\n",
                strerror(errno));
        util::exit_nicely();
    };
    if (read(node_cache_fd, &cacheHeader, sizeof(struct persistentCacheHeader))
            != sizeof(struct persistentCacheHeader))
    {
        fprintf(stderr, "Failed to read persistent cache header: %s\n",
                strerror(errno));
        util::exit_nicely();
    }
    if (cacheHeader.format_version != PERSISTENT_CACHE_FORMAT_VERSION)
    {
        fprintf(stderr, "Persistent cache header is wrong version\n");
        util::exit_nicely();
    }

    if (cacheHeader.id_size != sizeof(osmid_t))
    {
        fprintf(stderr, "Persistent cache header is wrong id type\n");
        util::exit_nicely();
    }

    fprintf(stderr,"Maximum node in persistent node cache: %" PRIdOSMID "\n", cacheHeader.max_initialised_id);

    readNodeBlockCache = new ramNodeBlock[READ_NODE_CACHE_SIZE];
    if (!readNodeBlockCache) {
        fprintf(stderr, "Out of memory: Failed to allocate node read cache\n");
        util::exit_nicely();
    }
}

node_persistent_cache::~node_persistent_cache()
{
    if (writeNodeBlock.dirty())
        nodes_set_create_writeout_block();

    writeout_dirty_nodes();

    if (writeNodeBlock.nodes)
        delete[] writeNodeBlock.nodes;

    if (lseek64(node_cache_fd, 0, SEEK_SET) < 0) {
        fprintf(stderr, "Failed to seek to correct position in node cache: %s\n",
                strerror(errno));
        util::exit_nicely();
    };
    if (write(node_cache_fd, &cacheHeader, sizeof(struct persistentCacheHeader))
            != sizeof(struct persistentCacheHeader))
    {
        fprintf(stderr, "Failed to update persistent cache header: %s\n",
                strerror(errno));
        util::exit_nicely();
    }
    fprintf(stderr,"Maximum node in persistent node cache: %" PRIdOSMID "\n", cacheHeader.max_initialised_id);

    fsync(node_cache_fd);

    if (close(node_cache_fd) != 0)
    {
        fprintf(stderr, "Failed to close node cache file: %s\n",
                strerror(errno));
    }

    if (readNodeBlockCache) {
        for (int i = 0; i < READ_NODE_CACHE_SIZE; i++)
        {
            if (readNodeBlockCache[i].nodes)
                delete[] readNodeBlockCache[i].nodes;
        }
        delete[] readNodeBlockCache;
    }
}
