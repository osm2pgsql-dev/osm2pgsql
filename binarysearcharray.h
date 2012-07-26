


struct key_val_tuple {
	int key;
	osmid_t value;
};

struct binary_search_array {
	int capacity;
	int size;
	struct key_val_tuple * array;
};

void binary_search_remove(struct binary_search_array * array, int key);
void binary_search_add(struct binary_search_array * array, int key, osmid_t value);
osmid_t binary_search_get(struct binary_search_array * array, int key);
struct binary_search_array *  init_search_array(int capacity);
void shutdown_search_array(struct binary_search_array ** array);

