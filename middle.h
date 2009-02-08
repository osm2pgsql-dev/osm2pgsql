/* Common middle layer interface */

/* Each middle layer data store must provide methods for 
 * storing and retrieving node and way data.
 */

#ifndef MIDDLE_H
#define MIDDLE_H

#include "osmtypes.h"

struct keyval;
struct member;
struct output_options;

struct middle_t {
    int (*start)(const struct output_options *options);
    void (*stop)(void);
    void (*cleanup)(void);
    void (*analyze)(void);
    void (*end)(void);

    int (*nodes_set)(int id, double lat, double lon, struct keyval *tags);
    int (*nodes_get_list)(struct osmNode *out, int *nds, int nd_count);
    int (*nodes_delete)(int id);
    int (*node_changed)(int id);
//    int (*nodes_get)(struct osmNode *out, int id);

    int (*ways_set)(int id, int *nds, int nd_count, struct keyval *tags, int pending);
    int (*ways_get)(int id, struct keyval *tag_ptr, struct osmNode **node_ptr, int *count_ptr);
    int (*ways_done)(int id);
    int (*ways_delete)(int id);
    int (*way_changed)(int id);

    int (*relations_set)(int id, struct member *members, int member_count, struct keyval *tags);
//    int (*relations_get)(int id, struct member **members, int *member_count, struct keyval *tags);
    int (*relations_done)(int id);
    int (*relations_delete)(int id);
    int (*relation_changed)(int id);

//    void (*iterate_nodes)(int (*callback)(int id, struct keyval *tags, double node_lat, double node_lon));
    void (*iterate_ways)(int (*callback)(int id, struct keyval *tags, struct osmNode *nodes, int count, int exists));
    void (*iterate_relations)(int (*callback)(int id, struct member *, int member_count, struct keyval *rel_tags, int exists));
};

#endif
