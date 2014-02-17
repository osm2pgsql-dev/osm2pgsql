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
    void * (*start)(const struct output_options *options);
    void * (*connect)(const struct output_options *options);
    void (*stop)(void);
    void (*cleanup)(void * thread_ctx);
    void (*analyze)(void);
    void (*end)(void);
    void (*commit)(void * thread_ctx);

    int (*nodes_set)(void * thread_ctx, osmid_t id, double lat, double lon, struct keyval *tags);
    int (*nodes_get_list)(void * thread_ctx, struct osmNode *out, osmid_t *nds, int nd_count);
    int (*nodes_delete)(void * thread_ctx, osmid_t id);
    int (*node_changed)(void * thread_ctx, osmid_t id);
    /* int (*nodes_get)(struct osmNode *out, osmid_t id);*/

    int (*ways_set)(void * thread_ctx, osmid_t id, osmid_t *nds, int nd_count, struct keyval *tags, int pending);
    int (*ways_get)(void * thread_ctx, osmid_t id, struct keyval *tag_ptr, struct osmNode **node_ptr, int *count_ptr);
    int (*ways_get_list)(void * thread_ctx, osmid_t *ids, int way_count, osmid_t **way_ids, struct keyval *tag_ptr, struct osmNode **node_ptr, int *count_ptr);

    int (*ways_done)(void * thread_ctx, osmid_t id);
    int (*ways_delete)(void * thread_ctx, osmid_t id);
    int (*way_changed)(void * thread_ctx, osmid_t id);

    int (*relations_set)(void * thread_ctx, osmid_t id, struct member *members, int member_count, struct keyval *tags);
    /* int (*relations_get)(osmid_t id, struct member **members, int *member_count, struct keyval *tags); */
    int (*relations_done)(void * thread_ctx, osmid_t id);
    int (*relations_delete)(void * thread_ctx, osmid_t id);
    int (*relation_changed)(void * thread_ctx, osmid_t id);

    /* void (*iterate_nodes)(int (*callback)(osmid_t id, struct keyval *tags, double node_lat, double node_lon)); */
    void (*iterate_ways)(int (*callback)(osmid_t id, struct keyval *tags, struct osmNode *nodes, int count, int exists));
    void (*iterate_relations)(int (*callback)(osmid_t id, struct member *, int member_count, struct keyval *rel_tags, int exists));
};

#endif
