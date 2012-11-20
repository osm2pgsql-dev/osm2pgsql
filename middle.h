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
    void (*commit)(void);

    int (*nodes_set)(osmid_t id, double lat, double lon, struct keyval *tags);
    int (*nodes_get_list)(struct osmNode *out, osmid_t *nds, int nd_count);
    int (*nodes_delete)(osmid_t id);
    int (*node_changed)(osmid_t id);
    /* int (*nodes_get)(struct osmNode *out, osmid_t id);*/

    int (*ways_set)(osmid_t id, osmid_t *nds, int nd_count, struct keyval *tags, int pending);
    int (*ways_get)(osmid_t id, struct keyval *tag_ptr, struct osmNode **node_ptr, int *count_ptr);
    int (*ways_get_list)(osmid_t *ids, int way_count, osmid_t **way_ids, struct keyval *tag_ptr, struct osmNode **node_ptr, int *count_ptr);

    int (*ways_done)(osmid_t id);
    int (*ways_delete)(osmid_t id);
    int (*way_changed)(osmid_t id);

    int (*relations_set)(osmid_t id, struct member *members, int member_count, struct keyval *tags);
    /* int (*relations_get)(osmid_t id, struct member **members, int *member_count, struct keyval *tags); */
    int (*relations_done)(osmid_t id);
    int (*relations_delete)(osmid_t id);
    int (*relation_changed)(osmid_t id);

    /* void (*iterate_nodes)(int (*callback)(osmid_t id, struct keyval *tags, double node_lat, double node_lon)); */
    void (*iterate_ways)(int (*callback)(osmid_t id, struct keyval *tags, struct osmNode *nodes, int count, int exists));
    void (*iterate_relations)(int (*callback)(osmid_t id, struct member *, int member_count, struct keyval *rel_tags, int exists));
};

#endif
