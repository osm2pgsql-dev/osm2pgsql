/* Implements the mid-layer processing for osm2pgsql
 * using data structures in RAM.
 *
 * This layer stores data read in from the planet.osm file
 * and is then read by the backend processing code to
 * emit the final geometry-enabled output formats
*/

#ifndef MIDDLE_RAM_H
#define MIDDLE_RAM_H

#include <memory>

#include "middle.hpp"
#include <vector>

struct node_ram_cache;
struct options_t;

struct middle_ram_t : public middle_t {
    middle_ram_t();
    virtual ~middle_ram_t();

    int start(const options_t *out_options_);
    void stop(void);
    void analyze(void);
    void end(void);
    void commit(void);

    int nodes_set(osmid_t id, double lat, double lon, const taglist_t &tags);
    int nodes_get_list(nodelist_t &out, const idlist_t nds) const;
    int nodes_delete(osmid_t id);
    int node_changed(osmid_t id);

    int ways_set(osmid_t id, const idlist_t &nds, const taglist_t &tags);
    int ways_get(osmid_t id, taglist_t &tags, nodelist_t &nodes) const;
    int ways_get_list(const idlist_t &ids, idlist_t &way_ids,
                      multitaglist_t &tags, multinodelist_t &nodes) const;

    int ways_delete(osmid_t id);
    int way_changed(osmid_t id);

    int relations_get(osmid_t id, memberlist_t &members, taglist_t &tags) const;
    int relations_set(osmid_t id, const memberlist_t &members, const taglist_t &tags);
    int relations_delete(osmid_t id);
    int relation_changed(osmid_t id);

    std::vector<osmid_t> relations_using_way(osmid_t way_id) const;

    void iterate_ways(middle_t::pending_processor& pf);
    void iterate_relations(pending_processor& pf);

    size_t pending_count() const;

    virtual boost::shared_ptr<const middle_query_t> get_instance() const;
private:

    void release_ways();
    void release_relations();

    struct ramWay {
        taglist_t tags;
        idlist_t ndids;
    };

    struct ramRel {
        taglist_t tags;
        memberlist_t members;
    };

    std::vector<std::vector<ramWay> > ways;
    std::vector<std::vector<ramRel> > rels;

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
