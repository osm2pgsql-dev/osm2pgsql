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
#include "output.h"
#include "node-persistent-cache.h"
#include "node-ram-cache.h"
#include "binarysearcharray.h"

void exit_nicely()
{
    fprintf(stderr, "Error occurred, cleaning up\n");
    exit(1);
}

int main(int argc, char *argv[]) {
	int i;
	struct output_options options;
	struct osmNode node;
	struct osmNode *nodes;
	osmid_t *osmids;
	int node_cnt;
	options.append = 1;
	options.scale = 100;
	options.flat_node_cache_enabled = 1;
	options.flat_node_file = argv[1];
	init_node_ram_cache(0,10,100);
	init_node_persistent_cache(&options);

	if (argc > 3) {
		node_cnt = argc - 2;
		nodes = malloc(sizeof(struct osmNode) * node_cnt);
		osmids = malloc(sizeof(osmid_t) * node_cnt);
		for (i = 0; i < node_cnt; i++) {
			osmids[i] = atoi(argv[2 + i]);
		}
		persistent_cache_nodes_get_list(nodes,osmids,node_cnt);
		for (i = 0; i < node_cnt; i++) {
			printf("lat: %f / lon: %f\n", nodes[i].lat, nodes[i].lon);
		}
	} else {
		if (strstr(argv[2],",") == NULL) {
			persistent_cache_nodes_get(&node, atoi(argv[2]));
			printf("lat: %f / lon: %f\n", node.lat, node.lon);
		} else {
			char * node_list = malloc(sizeof(char) * (strlen(argv[2]) + 1));
			strcpy(node_list,argv[2]);
			node_cnt = 1;
			strtok(node_list,",");
			while (strtok(NULL,",") != NULL) node_cnt++;
			printf("Processing %i nodes\n", node_cnt);
			nodes = malloc(sizeof(struct osmNode) * node_cnt);
			osmids = malloc(sizeof(osmid_t) * node_cnt);
			strcpy(node_list,argv[2]);
			osmids[0] = atoi(strtok(node_list,","));
			for (i = 1; i < node_cnt; i++) {
				char * tmp = strtok(NULL,",");
				osmids[i] = atoi(tmp);
			}
			persistent_cache_nodes_get_list(nodes,osmids,node_cnt);
			for (i = 0; i < node_cnt; i++) {
				printf("lat: %f / lon: %f\n", nodes[i].lat, nodes[i].lon);
			}
		}
	}


	shutdown_node_persistent_cache();
}
