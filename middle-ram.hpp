/* Implements the mid-layer processing for osm2pgsql
 * using data structures in RAM.
 * 
 * This layer stores data read in from the planet.osm file
 * and is then read by the backend processing code to
 * emit the final geometry-enabled output formats
*/
 
#ifndef MIDDLE_RAM_H
#define MIDDLE_RAM_H

#include "middle.hpp"
#include "node-ram-cache.hpp"
#include <memory>
#include <vector>

struct middle_ram_t : public middle_t {
    middle_ram_t();
    virtual ~middle_ram_t();

    int start(const options_t *out_options_);
    void stop(void);
    void cleanup(void);
    void analyze(void);
    void end(void);
    void commit(void);

    int nodes_set(osmid_t id, double lat, double lon, struct keyval *tags);
    int nodes_get_list(struct osmNode *out, osmid_t *nds, int nd_count) const;
    int nodes_delete(osmid_t id);
    int node_changed(osmid_t id);

    int ways_set(osmid_t id, osmid_t *nds, int nd_count, struct keyval *tags);
    int ways_get(osmid_t id, struct keyval *tag_ptr, struct osmNode **node_ptr, int *count_ptr) const;
    int ways_get_list(osmid_t *ids, int way_count, osmid_t **way_ids, struct keyval *tag_ptr, struct osmNode **node_ptr, int *count_ptr) const;

    int ways_delete(osmid_t id);
    int way_changed(osmid_t id);

    int relations_get(osmid_t id, struct member **members, int *member_count, struct keyval *tags) const;
    int relations_set(osmid_t id, struct member *members, int member_count, struct keyval *tags);
    int relations_delete(osmid_t id);
    int relation_changed(osmid_t id);

    std::vector<osmid_t> relations_using_way(osmid_t way_id) const;

    void iterate_ways(way_cb_func &cb);
    void iterate_relations(rel_cb_func &cb);

private:
    void release_ways();
    void release_relations();

    struct ramWay {
        struct keyval *tags;
        osmid_t *ndids;
        int pending;
    };
    
    struct ramRel {
        struct keyval *tags;
        struct member *members;
        int member_count;
    };

    std::vector<ramWay *> ways;
    std::vector<ramRel *> rels;

    int way_blocks;

    int way_out_count;
    int rel_out_count;

    std::auto_ptr<node_ram_cache> cache;

    /* the previous behaviour of iterate_ways was to delete all ways as they
     * were being iterated. this doesn't work now that the output handles its
     * own "done" status and output-specific "pending" status. however, the
     * tests depend on the behaviour that ways will be unavailable once
     * iterate_ways is complete, so this flag emulates that. */
    bool simulate_ways_deleted;
};

extern middle_ram_t mid_ram;

#endif
