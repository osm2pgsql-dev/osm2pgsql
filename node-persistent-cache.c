#define _LARGEFILE64_SOURCE     /* See feature_test_macrors(7) */

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

#include "osmtypes.h"
#include "node-persistent-cache.h"
#include "node-ram-cache.h"

static int node_cache_fd;
static char * node_cache_fname;
static int append_mode;

struct persistentCacheHeader cacheHeader;
static struct ramNodeBlock writeNodeBlock;
static struct ramNodeBlock * readNodeBlockCache;

static int scale;


static int writeout_dirty_nodes(osmid_t id) {

	if (writeNodeBlock.dirty > 0) {
		lseek64(node_cache_fd, (writeNodeBlock.block_offset << WRITE_NODE_BLOCK_SHIFT) * sizeof(struct ramNode) + sizeof(struct persistentCacheHeader), SEEK_SET);
		if (write(node_cache_fd, writeNodeBlock.nodes, WRITE_NODE_BLOCK_SIZE * sizeof(struct ramNode))
				< WRITE_NODE_BLOCK_SIZE * sizeof(struct ramNode)) {
			fprintf(stderr, "Failed to write out node cache: %s\n", strerror(errno));
			exit(1);
		}
		cacheHeader.max_initialised_id = ((writeNodeBlock.block_offset + 1) << WRITE_NODE_BLOCK_SHIFT) - 1;
		writeNodeBlock.used = 0;
		writeNodeBlock.dirty = 0;
		lseek64(node_cache_fd, 0, SEEK_SET);
		if (write(node_cache_fd, &cacheHeader, sizeof(struct persistentCacheHeader)) != sizeof(struct persistentCacheHeader)) {
			fprintf(stderr, "Failed to update persistent cache header: %s\n", strerror(errno));
			exit(1);
		}
	}
	if (id < 0) {
		for (int i = 0; i < READ_NODE_CACHE_SIZE; i++) {
			if (readNodeBlockCache[i].dirty) {
				lseek64(node_cache_fd, (readNodeBlockCache[i].block_offset << READ_NODE_BLOCK_SHIFT) * sizeof(struct ramNode) + sizeof(struct persistentCacheHeader), SEEK_SET);
				if (write(node_cache_fd, readNodeBlockCache[i].nodes, READ_NODE_BLOCK_SIZE * sizeof(struct ramNode))
						< READ_NODE_BLOCK_SIZE * sizeof(struct ramNode)) {
					fprintf(stderr, "Failed to write out node cache: %s\n", strerror(errno));
					exit(1);
				}
			}
			readNodeBlockCache[i].dirty = 0;
		}
	}

}

static void ramNodes_clear(struct ramNode * nodes, int size) {
	for (int i = 0; i < size; i++) {
		#ifdef FIXED_POINT
			nodes[i].lon = INT_MIN;
			nodes[i].lat = INT_MIN;
		#else
			nodes[i].lon = NaN;
			nodes[i].lat = NaN;
		#endif
	}
}

static int persistent_cache_find_block(osmid_t block_offset) {
	int block_id = -1;
	for (int i = 0; i < READ_NODE_CACHE_SIZE; i++) {
		if (readNodeBlockCache[i].block_offset == block_offset) {
			block_id = i;
			break;
		}
	}
	return block_id;
}

static int persistent_cache_load_block(osmid_t block_offset) {
	int min_used = INT_MAX;
	int block_id = -1;
	for (int i = 0; i < READ_NODE_CACHE_SIZE; i++) {
		if (readNodeBlockCache[i].used < min_used) {
			min_used = readNodeBlockCache[i].used;
			block_id = i;
		}
	}
	if (min_used > 0) {
		for (int i = 0; i < READ_NODE_CACHE_SIZE; i++) {
			if (readNodeBlockCache[i].used > 1) {
				readNodeBlockCache[i].used--;
			}
		}
	}
	if (readNodeBlockCache[block_id].dirty) {
		lseek64(node_cache_fd, (readNodeBlockCache[block_id].block_offset << READ_NODE_BLOCK_SHIFT) * sizeof(struct ramNode) + sizeof(struct persistentCacheHeader), SEEK_SET);
		if (write(node_cache_fd, readNodeBlockCache[block_id].nodes, READ_NODE_BLOCK_SIZE * sizeof(struct ramNode))
				< READ_NODE_BLOCK_SIZE * sizeof(struct ramNode)) {
			fprintf(stderr, "Failed to write out node cache: %s\n", strerror(errno));
			exit(1);
		}
		readNodeBlockCache[block_id].dirty = 0;
	}

	ramNodes_clear(readNodeBlockCache[block_id].nodes, READ_NODE_BLOCK_SIZE);
	readNodeBlockCache[block_id].block_offset = block_offset;
	readNodeBlockCache[block_id].used = READ_NODE_CACHE_SIZE;

	//Make sure the node cache is correctly initialised for the block that will be read
	if (cacheHeader.max_initialised_id < ((block_offset + 1) << READ_NODE_BLOCK_SHIFT)) {
		//Need to expand the persistent node cache
		lseek64(node_cache_fd, cacheHeader.max_initialised_id * sizeof(struct ramNode) + sizeof(struct persistentCacheHeader), SEEK_SET);
		for (int i = cacheHeader.max_initialised_id >> READ_NODE_BLOCK_SHIFT; i <= block_offset; i++) {
			if (write(node_cache_fd, readNodeBlockCache[block_id].nodes, READ_NODE_BLOCK_SIZE * sizeof(struct ramNode))
					< READ_NODE_BLOCK_SIZE * sizeof(struct ramNode)) {
				fprintf(stderr, "Failed to write out node cache: %s\n", strerror(errno));
				exit(1);
			}
		}
		cacheHeader.max_initialised_id = ((block_offset + 1) << READ_NODE_BLOCK_SHIFT) - 1;
		lseek64(node_cache_fd, 0, SEEK_SET);
		if (write(node_cache_fd, &cacheHeader, sizeof(struct persistentCacheHeader)) != sizeof(struct persistentCacheHeader)) {
			fprintf(stderr, "Failed to update persistent cache header: %s\n", strerror(errno));
			exit(1);
		}
	}

	//Read the block into cache
	lseek64(node_cache_fd, (block_offset << READ_NODE_BLOCK_SHIFT) * sizeof(struct ramNode) + sizeof(struct persistentCacheHeader), SEEK_SET);
	if (read(node_cache_fd, readNodeBlockCache[block_id].nodes, READ_NODE_BLOCK_SIZE * sizeof(struct ramNode)) !=
			READ_NODE_BLOCK_SIZE * sizeof(struct ramNode)) {
		fprintf(stderr, "Failed to read from node cache: %s\n", strerror(errno));
		exit(1);
	}
    //    fprintf(stderr, "Load block %i %i\n", block_id, block_offset);

	return block_id;
}

int persistent_cache_nodes_set_create(osmid_t id, double lat, double lon) {
	osmid_t block_offset = id >> WRITE_NODE_BLOCK_SHIFT;

	if (writeNodeBlock.block_offset != block_offset) {
		if (writeNodeBlock.dirty) {
			if (write(node_cache_fd, writeNodeBlock.nodes, WRITE_NODE_BLOCK_SIZE * sizeof(struct ramNode))
					< WRITE_NODE_BLOCK_SIZE * sizeof(struct ramNode)) {
				fprintf(stderr, "Failed to write out node cache: %s\n", strerror(errno));
				exit(1);
			}
			writeNodeBlock.used = 0;
			writeNodeBlock.dirty = 0;
			//After writing out the node block, the file pointer is at the next block level
			writeNodeBlock.block_offset++;
			cacheHeader.max_initialised_id = (writeNodeBlock.block_offset << WRITE_NODE_BLOCK_SHIFT) - 1;
		}
		if (writeNodeBlock.block_offset > block_offset) {
			fprintf(stderr, "ERROR: Block_offset not in sequential order: %i %i\n", writeNodeBlock.block_offset, block_offset);
			exit(1);
		}

		//We need to fill the intermediate node cache with node nodes to identify which nodes are valid
		for (int i = writeNodeBlock.block_offset; i < block_offset; i++) {
			ramNodes_clear(writeNodeBlock.nodes, WRITE_NODE_BLOCK_SIZE);
			if (write(node_cache_fd, writeNodeBlock.nodes, WRITE_NODE_BLOCK_SIZE * sizeof(struct ramNode))
					< WRITE_NODE_BLOCK_SIZE * sizeof(struct ramNode)) {
				fprintf(stderr, "Failed to write out node cache: %s\n", strerror(errno));
				exit(1);
			}
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

int persistent_cache_nodes_set_append(osmid_t id, double lat, double lon) {
	osmid_t block_offset = id >> READ_NODE_BLOCK_SHIFT;

	int block_id = persistent_cache_find_block(block_offset);

	if (block_id < 0) block_id = persistent_cache_load_block(block_offset);

#ifdef FIXED_POINT
	if (isnan(lat) && isnan(lon)) {
		readNodeBlockCache[block_id].nodes[id & READ_NODE_BLOCK_MASK].lat = INT_MIN;
		readNodeBlockCache[block_id].nodes[id & READ_NODE_BLOCK_MASK].lon = INT_MIN;
	} else {
		readNodeBlockCache[block_id].nodes[id & READ_NODE_BLOCK_MASK].lat = DOUBLE_TO_FIX(lat);
		readNodeBlockCache[block_id].nodes[id & READ_NODE_BLOCK_MASK].lon = DOUBLE_TO_FIX(lon);
	}
#else
	readNodeBlockCache[block_id].nodes[id & READ_NODE_BLOCK_MASK].lat = lat;
	readNodeBlockCache[block_id].nodes[id & READ_NODE_BLOCK_MASK].lon = lon;
#endif
	readNodeBlockCache[block_id].used++;
	readNodeBlockCache[block_id].dirty = 1;

}

int persistent_cache_nodes_get(struct osmNode *out, osmid_t id) {
	osmid_t block_offset = id >> READ_NODE_BLOCK_SHIFT;

	osmid_t block_id = persistent_cache_find_block(block_offset);

	if (block_id < 0) {
		writeout_dirty_nodes(id);
		block_id = persistent_cache_load_block(block_offset);
	}

	readNodeBlockCache[block_id].used++;

#ifdef FIXED_POINT
	if ((readNodeBlockCache[block_id].nodes[id & READ_NODE_BLOCK_MASK].lat == INT_MIN) &&
			(readNodeBlockCache[block_id].nodes[id & READ_NODE_BLOCK_MASK].lon == INT_MIN)) {
		return 1;
	} else {
		out->lat = FIX_TO_DOUBLE(readNodeBlockCache[block_id].nodes[id & READ_NODE_BLOCK_MASK].lat);
		out->lon = FIX_TO_DOUBLE(readNodeBlockCache[block_id].nodes[id & READ_NODE_BLOCK_MASK].lon);
		return 0;
	}
#else
	if ((isnan(readNodeBlockCache[block_id].nodes[id & READ_NODE_BLOCK_MASK].lat)) &&
			(isnan(readNodeBlockCache[block_id].nodes[id & READ_NODE_BLOCK_MASK].lon))) {
		return 1;
	} else {
		out->lat = readNodeBlockCache[block_id].nodes[id & READ_NODE_BLOCK_MASK].lat;
		out->lon = readNodeBlockCache[block_id].nodes[id & READ_NODE_BLOCK_MASK].lon;
		return 0;
	}
#endif

	return 0;
}

void init_node_persistent_cache(int mode, int fixpointscale) {
	scale = fixpointscale;
	append_mode = mode;
	node_cache_fname = "osm2pgsql-node.cache";
	fprintf(stderr, "Mid: loading persistent node cache from %s\n", node_cache_fname);
	/* Setup the file for the node position cache */
	if (append_mode) {
		node_cache_fd = open(node_cache_fname, O_RDWR, S_IRUSR | S_IWUSR);
		if (node_cache_fd < 0) {
			fprintf(stderr, "Failed to open node cache file: %s\n", strerror(errno));
			exit(1);
		}
	} else {
		node_cache_fd = open(node_cache_fname,O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
		if (node_cache_fd < 0) {
			fprintf(stderr, "Failed to create node cache file: %s\n", strerror(errno));
			exit(1);
		}
		lseek64(node_cache_fd, 0, SEEK_SET);
		if (posix_fallocate(node_cache_fd, 0, sizeof(struct ramNode) * MAXIMUM_INITIAL_ID) != 0) {
			fprintf(stderr, "Failed to allocate space for node cache file: %s\n", strerror(errno));
			close(node_cache_fd);
		}
		fprintf(stderr, "Allocated space for persistent node cache file\n");
		writeNodeBlock.nodes = malloc(WRITE_NODE_BLOCK_SIZE * sizeof(struct ramNode));
		ramNodes_clear(writeNodeBlock.nodes, WRITE_NODE_BLOCK_SIZE);
		writeNodeBlock.block_offset = 0;
		writeNodeBlock.used = 0;
		writeNodeBlock.dirty = 0;
		cacheHeader.format_version = PERSISTENT_CACHE_FORMAT_VERSION;
		cacheHeader.id_size = sizeof(osmid_t);
		cacheHeader.max_initialised_id = 0;
		lseek64(node_cache_fd, 0, SEEK_SET);
		if (write(node_cache_fd, &cacheHeader, sizeof(struct persistentCacheHeader)) != sizeof(struct persistentCacheHeader)) {
			fprintf(stderr, "Failed to write persistent cache header: %s\n", strerror(errno));
			exit(1);
		}

	}
	lseek64(node_cache_fd, 0, SEEK_SET);
	if (read(node_cache_fd, &cacheHeader, sizeof(struct persistentCacheHeader)) != sizeof(struct persistentCacheHeader)) {
		fprintf(stderr, "Failed to read persistent cache header: %s\n", strerror(errno));
		exit(1);
	}
	if (cacheHeader.format_version != PERSISTENT_CACHE_FORMAT_VERSION) {
		fprintf(stderr, "Persistent cache header is wrong version\n");
		exit(1);
	}

	if (cacheHeader.id_size != sizeof(osmid_t)) {
		fprintf(stderr, "Persistent cache header is wrong id type\n");
		exit(1);
	}

	fprintf(stderr,"Maximum node in persistent node cache: %li\n", cacheHeader.max_initialised_id);

	readNodeBlockCache = malloc(READ_NODE_CACHE_SIZE*sizeof(struct ramNodeBlock));
	for (int i = 0 ; i < READ_NODE_CACHE_SIZE; i++) {
		readNodeBlockCache[i].nodes = malloc(READ_NODE_BLOCK_SIZE * sizeof(struct ramNode));
		readNodeBlockCache[i].block_offset = -1;
		readNodeBlockCache[i].used = 0;
		readNodeBlockCache[i].dirty = 0;
	}
}

void shutdown_node_persistent_cache() {
	writeout_dirty_nodes(-1);

	lseek64(node_cache_fd, 0, SEEK_SET);
	if (write(node_cache_fd, &cacheHeader, sizeof(struct persistentCacheHeader)) != sizeof(struct persistentCacheHeader)) {
		fprintf(stderr, "Failed to update persistent cache header: %s\n", strerror(errno));
		exit(1);
	}
	fprintf(stderr,"Maximum node in persistent node cache: %li\n", cacheHeader.max_initialised_id);

	fsync(node_cache_fd);

	if (close(node_cache_fd) != 0) {
		fprintf(stderr, "Failed to close node cache file: %s\n", strerror(errno));
	}

	for (int i = 0 ; i < READ_NODE_CACHE_SIZE; i++) {
		free(readNodeBlockCache[i].nodes);
	}
	free(readNodeBlockCache);
	readNodeBlockCache = NULL;
}
