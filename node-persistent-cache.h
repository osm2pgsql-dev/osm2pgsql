#define MAXIMUM_INITIAL_ID 2600000000

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

struct node_persistent_thread_ctx {
    int node_cache_fd;
    struct ramNodeBlock * readNodeBlockCache;
    struct binary_search_array * readNodeBlockCacheIdx;
};

int persistent_cache_nodes_set(void * ctx_p, osmid_t id, double lat, double lon);
int persistent_cache_nodes_get(void * ctx_p, struct osmNode *out, osmid_t id);
int persistent_cache_nodes_get_list(void * ctx_p, struct osmNode *nodes, osmid_t *ndids, int nd_count);
void writeout_dirty_nodes(void * ctx_p, osmid_t id);
void * init_node_persistent_cache(const struct output_options *options, const int append);
void shutdown_node_persistent_cache(void * ctx_p);
