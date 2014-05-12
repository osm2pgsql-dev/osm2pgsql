/* Common middle layer interface */

/* Each middle layer data store must provide methods for 
 * storing and retrieving node and way data.
 */

#ifndef MIDDLE_H
#define MIDDLE_H

#include "osmtypes.hpp"

struct keyval;
struct member;
struct output_options;

struct middle_t {
    virtual ~middle_t();

    virtual int start(const struct output_options *options) = 0;
    virtual void stop(void) = 0;
    virtual void cleanup(void) = 0;
    virtual void analyze(void) = 0;
    virtual void end(void) = 0;
    virtual void commit(void) = 0;

    virtual int nodes_set(osmid_t id, double lat, double lon, struct keyval *tags) = 0;
    virtual int nodes_get_list(struct osmNode *out, osmid_t *nds, int nd_count) = 0;

    virtual int ways_set(osmid_t id, osmid_t *nds, int nd_count, struct keyval *tags, int pending) = 0;
    virtual int ways_get(osmid_t id, struct keyval *tag_ptr, struct osmNode **node_ptr, int *count_ptr) = 0;
    virtual int ways_get_list(osmid_t *ids, int way_count, osmid_t **way_ids, struct keyval *tag_ptr, struct osmNode **node_ptr, int *count_ptr) = 0;

    virtual int ways_done(osmid_t id) = 0;

    virtual int relations_set(osmid_t id, struct member *members, int member_count, struct keyval *tags) = 0;

    typedef int (*way_callback)(osmid_t id, struct keyval *tags, struct osmNode *nodes, int count, int exists);
    typedef int (*relation_callback)(osmid_t id, struct member *, int member_count, struct keyval *rel_tags, int exists);

    virtual void iterate_ways(way_callback callback) = 0;
    virtual void iterate_relations(relation_callback callback) = 0;
};

struct slim_middle_t : public middle_t {
    virtual ~slim_middle_t();

    virtual int nodes_delete(osmid_t id) = 0;
    virtual int node_changed(osmid_t id) = 0;
    virtual int ways_delete(osmid_t id) = 0;
    virtual int way_changed(osmid_t id) = 0;

    virtual int relations_done(osmid_t id) = 0;
    virtual int relations_delete(osmid_t id) = 0;
    virtual int relation_changed(osmid_t id) = 0;
};

#endif
