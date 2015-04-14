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
#include "options.hpp"
#include "node-persistent-cache.hpp"
#include "node-ram-cache.hpp"

void test_get_node_list(boost::shared_ptr<node_persistent_cache> cache,
                        int itterations, int max_size, int process_number) {
    int i, j, node_cnt, node_cnt_total;
    nodelist_t nodes;
    struct timeval start, stop;
    struct timeval start_overall, stop_overall;
    idlist_t osmids;

    node_cnt_total = 0;
    gettimeofday(&start_overall, NULL);
    for (i = 0; i < itterations; i++) {
        node_cnt = random() % max_size;
        node_cnt_total += node_cnt;

        printf("Process %i: Getting %i nodes....\n", process_number, node_cnt);
        for (j = 0; j < node_cnt; j++) {
            osmids.push_back(random() % (1 << 31));
        }
        gettimeofday(&start, NULL);
        cache->get_list(nodes,osmids);
        gettimeofday(&stop, NULL);
        double duration = ((stop.tv_sec - start.tv_sec)*1000000.0 + (stop.tv_usec - start.tv_usec))/1000000.0;
        printf("Process %i: Got nodes in %f at a rate of %f/s\n", process_number, duration, node_cnt / duration);
        nodes.clear();
        osmids.clear();
    }
    gettimeofday(&stop_overall, NULL);
    double duration = ((stop_overall.tv_sec - start_overall.tv_sec)*1000000.0 + (stop_overall.tv_usec - start_overall.tv_usec))/1000000.0;
    printf("Process %i: Got a total of nodes in %f at a rate of %f/s\n", process_number, duration, node_cnt_total / duration);
}

int main(int argc, char *argv[]) {
	int i,p;
	options_t options;
	osmNode node;
	nodelist_t nodes;
	struct timeval start;
	idlist_t osmids;
	int node_cnt;
	options.append = 1;
	options.flat_node_cache_enabled = 1;
	options.flat_node_file = argv[1];
        boost::shared_ptr<node_ram_cache> ram_cache(new node_ram_cache(0, 10, options.scale));
        boost::shared_ptr<node_persistent_cache> cache;


	if (argc > 3) {
                cache.reset(new node_persistent_cache(&options, 1, ram_cache));
		node_cnt = argc - 2;
		for (i = 0; i < node_cnt; i++) {
			osmids.push_back(atoi(argv[2 + i]));
		}
		cache->get_list(nodes, osmids);
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
			strcpy(node_list,argv[2]);
			osmids.push_back(atoi(strtok(node_list,",")));
			for (i = 1; i < node_cnt; i++) {
				char * tmp = strtok(NULL,",");
				osmids.push_back(atoi(tmp));
			}
			cache->get_list(nodes,osmids);
			for (i = 0; i < node_cnt; i++) {
				printf("lat: %f / lon: %f\n", nodes[i].lat, nodes[i].lon);
			}
		}
	}


        cache.reset();
        ram_cache.reset();

    return 0;
}
