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

    int start(const struct output_options *options);
    void stop(void);
    void cleanup(void);
    void analyze(void);
    void end(void);
    void commit(void);

    int nodes_set(osmid_t id, double lat, double lon, struct keyval *tags);
    int nodes_get_list(struct osmNode *out, osmid_t *nds, int nd_count);
    int nodes_delete(osmid_t id);
    int node_changed(osmid_t id);

    int ways_set(osmid_t id, osmid_t *nds, int nd_count, struct keyval *tags, int pending);
    int ways_get(osmid_t id, struct keyval *tag_ptr, struct osmNode **node_ptr, int *count_ptr);
    int ways_get_list(osmid_t *ids, int way_count, osmid_t **way_ids, struct keyval *tag_ptr, struct osmNode **node_ptr, int *count_ptr);

    int ways_done(osmid_t id);
    int ways_delete(osmid_t id);
    int way_changed(osmid_t id);

    int relations_get(osmid_t id, struct member **members, int *member_count, struct keyval *tags);
    int relations_set(osmid_t id, struct member *members, int member_count, struct keyval *tags);
    int relations_done(osmid_t id);
    int relations_delete(osmid_t id);
    int relation_changed(osmid_t id);

    void iterate_ways(way_cb_func &cb);
    void iterate_relations(rel_cb_func &cb);

private:
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
};

extern middle_ram_t mid_ram;

#endif
