#define MAXIMUM_INITIAL_ID (1L << 31)

#define READ_NODE_CACHE_SIZE 10000
#define READ_NODE_BLOCK_SHIFT 10l
#define READ_NODE_BLOCK_SIZE (1l << READ_NODE_BLOCK_SHIFT)
#define READ_NODE_BLOCK_MASK 0x03FFl

#define WRITE_NODE_BLOCK_SHIFT 20l
#define WRITE_NODE_BLOCK_SIZE (1l << WRITE_NODE_BLOCK_SHIFT)
#define WRITE_NODE_BLOCK_MASK 0x0FFFFFl

#define PERSISTENT_CACHE_FORMAT_VERSION 1

struct persistentCacheHeader {
	int format_version;
	int id_size;
    osmid_t max_initialised_id;
};

int persistent_cache_nodes_set_append(osmid_t id, double lat, double lon);
int persistent_cache_nodes_set_create(osmid_t id, double lat, double lon);
int persistent_cache_nodes_get(struct osmNode *out, osmid_t id);
void init_node_persistent_cache(int mode, int fixpointscale);
void shutdown_node_persistent_cache();
