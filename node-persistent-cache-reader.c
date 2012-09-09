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
#include <sys/wait.h>
#include <sys/time.h>

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

void test_get_node_list(int itterations, int max_size, int process_number) {
    int i, node_cnt, node_cnt_total;
    struct osmNode *nodes;
    struct timeval start, stop;
    struct timeval start_overall, stop_overall;
    osmid_t *osmids;

    node_cnt_total = 0;
    gettimeofday(&start_overall, NULL);
    for (i = 0; i < itterations; i++) {
        node_cnt = random() % max_size;
        node_cnt_total += node_cnt;

        printf("Process %i: Getting %i nodes....\n", process_number, node_cnt);
        nodes = malloc(sizeof(struct osmNode) * node_cnt);
        osmids = malloc(sizeof(osmid_t) * node_cnt);
        for (int j = 0; j < node_cnt; j++) {
            osmids[j] = random() % (1 << 31);
        }
        gettimeofday(&start, NULL);
        persistent_cache_nodes_get_list(nodes,osmids,node_cnt);
        gettimeofday(&stop, NULL);
        double duration = ((stop.tv_sec - start.tv_sec)*1000000.0 + (stop.tv_usec - start.tv_usec))/1000000.0;
        printf("Process %i: Got nodes in %f at a rate of %f/s\n", process_number, duration, node_cnt / duration);
    }
    gettimeofday(&stop_overall, NULL);
    double duration = ((stop_overall.tv_sec - start_overall.tv_sec)*1000000.0 + (stop_overall.tv_usec - start_overall.tv_usec))/1000000.0;
    printf("Process %i: Got a total of nodes in %f at a rate of %f/s\n", process_number, duration, node_cnt_total / duration);
}

int main(int argc, char *argv[]) {
	int i,p;
	struct output_options options;
	struct osmNode node;
	struct osmNode *nodes;
	struct timeval start, stop;
	osmid_t *osmids;
	int node_cnt;
	options.append = 1;
	options.scale = 100;
	options.flat_node_cache_enabled = 1;
	options.flat_node_file = argv[1];
	init_node_ram_cache(0,10,100);


	if (argc > 3) {
	    init_node_persistent_cache(&options, 1);
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
	} else if (argc == 2) {
        char state[128];
        gettimeofday(&start, NULL);
        initstate(start.tv_usec, &state, 8);
        setstate(&state);

	    printf("Testing mode\n");
	    init_node_persistent_cache(&options, 1);
	    test_get_node_list(10, 200, 0);
	    shutdown_node_persistent_cache();
	    printf("Testing using multiple processes\n");
	    int noProcs = 4;
	    int pid;
	    for (p = 1; p < noProcs; p++) {
	        pid=fork();
	        if (pid==0) {
	            break;
	        }
	        if (pid==-1) {
	            fprintf(stderr,"WARNING: Failed to fork helper processes. Falling back to only using %i \n", p);
	            exit(1);
	        }
	    }
	    gettimeofday(&start, NULL);
	    initstate(start.tv_usec, &state, 8);
	    setstate(&state);
	    init_node_persistent_cache(&options, 1);
	    test_get_node_list(10,200,p);

	    if (pid == 0) {
	        shutdown_node_persistent_cache();
	        fprintf(stderr,"Exiting process %i\n", p);
	        exit(0);
	    } else {
	        //fprintf(stderr,"Counting %i\n", p);
	        //for (long k = 0; k < (1l << 60); k++) if (k == (1l<<50)) printf(stderr,"SHouldn't be here");
	        //fprintf(stderr,"Counted\n", p);

	        for (p = 0; p < noProcs; p++) wait(NULL);
	    }
	    fprintf(stderr, "\nAll child processes exited\n");
	} else {
	    init_node_persistent_cache(&options, 1);
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
