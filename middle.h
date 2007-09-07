/* Common middle layer interface */

/* Each middle layer data store must provide methods for 
 * storing and retrieving node, segment and way data.
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
    int (*segments_set)(int id, int from, int to, struct keyval *tags);
    int (*segments_get)(struct osmSegment *out, int id);
    int (*nodes_set)(int id, double lat, double lon, struct keyval *tags);
    int (*nodes_get)(struct osmNode *out, int id);
    int (*ways_set)(int id, struct keyval *segs, struct keyval *tags);
    int *(*ways_get)(int id);
    void (*iterate_nodes)(int (*callback)(int id, struct keyval *tags, double node_lat, double node_lon));
    void (*iterate_ways)(int (*callback)(int id, struct keyval *tags, struct osmSegLL *segll, int count));
};

#endif
