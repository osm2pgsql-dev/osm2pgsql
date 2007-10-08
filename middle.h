/* Common middle layer interface */

/* Each middle layer data store must provide methods for 
 * storing and retrieving node and way data.
 */

#ifndef MIDDLE_H
#define MIDDLE_H

#include "keyvals.h"

struct middle_t {
    int (*start)(const char *db, int latlong);
    void (*stop)(void);
    void (*cleanup)(void);
    void (*analyze)(void);
    void (*end)(void);
    int (*nodes_set)(int id, double lat, double lon, struct keyval *tags);
    int (*nodes_get)(struct osmNode *out, int id);
    int (*ways_set)(int id, struct keyval *segs, struct keyval *tags);
    int *(*ways_get)(int id);
    int (*relations_set)(int id, struct keyval *members, struct keyval *tags);
    void (*iterate_nodes)(int (*callback)(int id, struct keyval *tags, double node_lat, double node_lon));
    void (*iterate_ways)(int (*callback)(int id, struct keyval *tags, struct osmNode *nodes, int count));
    void (*iterate_relations)(int (*callback)(int id, struct keyval *rel_tags, struct osmNode **nodes, struct keyval **tags, int *count));
};

#endif
