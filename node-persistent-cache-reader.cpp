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

#include "osmtypes.hpp"
#include "output.hpp"
#include "node-persistent-cache.hpp"
#include "node-ram-cache.hpp"
#include "binarysearcharray.hpp"

void exit_nicely()
{
    fprintf(stderr, "Error occurred, cleaning up\n");
    exit(1);
}

void test_get_node_list(boost::shared_ptr<node_persistent_cache> cache,
                        int itterations, int max_size, int process_number) {
    int i, j, node_cnt, node_cnt_total;
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
        nodes = (struct osmNode *)malloc(sizeof(struct osmNode) * node_cnt);
        osmids = (osmid_t *)malloc(sizeof(osmid_t) * node_cnt);
        for (j = 0; j < node_cnt; j++) {
            osmids[j] = random() % (1 << 31);
        }
        gettimeofday(&start, NULL);
        cache->get_list(nodes,osmids,node_cnt);
        gettimeofday(&stop, NULL);
        double duration = ((stop.tv_sec - start.tv_sec)*1000000.0 + (stop.tv_usec - start.tv_usec))/1000000.0;
        printf("Process %i: Got nodes in %f at a rate of %f/s\n", process_number, duration, node_cnt / duration);
        free(nodes);
        free(osmids);
    }
    gettimeofday(&stop_overall, NULL);
    double duration = ((stop_overall.tv_sec - start_overall.tv_sec)*1000000.0 + (stop_overall.tv_usec - start_overall.tv_usec))/1000000.0;
    printf("Process %i: Got a total of nodes in %f at a rate of %f/s\n", process_number, duration, node_cnt_total / duration);
}

int main(int argc, char *argv[]) {
	int i,p;
	options_t options;
	struct osmNode node;
	struct osmNode *nodes;
	struct timeval start;
	osmid_t *osmids;
	int node_cnt;
	options.append = 1;
	options.flat_node_cache_enabled = 1;
	options.flat_node_file = argv[1];
        boost::shared_ptr<node_ram_cache> ram_cache(new node_ram_cache(0, 10, options.scale));
        boost::shared_ptr<node_persistent_cache> cache;


	if (argc > 3) {
                cache.reset(new node_persistent_cache(&options, 1, ram_cache));
		node_cnt = argc - 2;
		nodes = (struct osmNode *)malloc(sizeof(struct osmNode) * node_cnt);
		osmids = (osmid_t *)malloc(sizeof(osmid_t) * node_cnt);
		for (i = 0; i < node_cnt; i++) {
			osmids[i] = atoi(argv[2 + i]);
		}
		cache->get_list(nodes,osmids,node_cnt);
		for (i = 0; i < node_cnt; i++) {
			printf("lat: %f / lon: %f\n", nodes[i].lat, nodes[i].lon);
		}
	} else if (argc == 2) {
            char * state = (char *)malloc(sizeof(char)* 128);
        gettimeofday(&start, NULL);
        initstate(start.tv_usec, state, 8);
        setstate(state);

	    printf("Testing mode\n");
            cache.reset(new node_persistent_cache(&options, 1, ram_cache));
	    test_get_node_list(cache, 10, 200, 0);
            cache.reset();
#ifdef HAVE_FORK
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
	    initstate(start.tv_usec, state, 8);
	    setstate(state);
            cache.reset(new node_persistent_cache(&options, 1, ram_cache));
	    test_get_node_list(cache, 10,200,p);

	    if (pid == 0) {
                cache.reset();
	        fprintf(stderr,"Exiting process %i\n", p);
	        exit(0);
	    } else {
	        for (p = 0; p < noProcs; p++) wait(NULL);
	    }
        free(state);
	    fprintf(stderr, "\nAll child processes exited\n");
#endif
	} else {
                cache.reset(new node_persistent_cache(&options, 1, ram_cache));
		if (strstr(argv[2],",") == NULL) {
			cache->get(&node, atoi(argv[2]));
			printf("lat: %f / lon: %f\n", node.lat, node.lon);
		} else {
                    char * node_list = (char *)malloc(sizeof(char) * (strlen(argv[2]) + 1));
			strcpy(node_list,argv[2]);
			node_cnt = 1;
			strtok(node_list,",");
			while (strtok(NULL,",") != NULL) node_cnt++;
			printf("Processing %i nodes\n", node_cnt);
			nodes = (struct osmNode *)malloc(sizeof(struct osmNode) * node_cnt);
			osmids = (osmid_t *)malloc(sizeof(osmid_t) * node_cnt);
			strcpy(node_list,argv[2]);
			osmids[0] = atoi(strtok(node_list,","));
			for (i = 1; i < node_cnt; i++) {
				char * tmp = strtok(NULL,",");
				osmids[i] = atoi(tmp);
			}
			cache->get_list(nodes,osmids,node_cnt);
			for (i = 0; i < node_cnt; i++) {
				printf("lat: %f / lon: %f\n", nodes[i].lat, nodes[i].lon);
			}
		}
	}


        cache.reset();
        ram_cache.reset();

    return 0;
}
