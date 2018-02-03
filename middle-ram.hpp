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
#include <array>

struct node_ram_cache;
struct options_t;

template <typename T, size_t N>
class cache_block_t
{
    std::array<std::unique_ptr<T>, N> arr;
public:
    void set(size_t idx, T *ele) { arr[idx].reset(ele); }

    T const *get(size_t idx) const { return arr[idx].get(); }
};

template <typename T, size_t BLOCK_SHIFT>
class elem_cache_t
{
    constexpr static size_t per_block() { return 1 << BLOCK_SHIFT; }
    constexpr static size_t num_blocks() { return 1 << (32 - BLOCK_SHIFT); }

    constexpr static size_t id2block(osmid_t id)
    {
        /* + NUM_BLOCKS/2 allows for negative IDs */
        return (id >> BLOCK_SHIFT) + num_blocks()/2;
    }

    constexpr static size_t id2offset(osmid_t id)
    {
        return id & (per_block()-1);
    }

    typedef cache_block_t<T, 1 << BLOCK_SHIFT> element_t;
    std::vector<std::unique_ptr<element_t>> arr;
public:
    elem_cache_t() : arr(num_blocks()) {}

    void set(osmid_t id, T *ele)
    {
        const size_t block = id2block(id);

        if (!arr[block]) {
            arr[block].reset(new element_t());
        }

        arr[block]->set(id2offset(id), ele);
    }

    T const *get(osmid_t id) const
    {
        const size_t block = id2block(id);

        if (!arr[block]) {
            return 0;
        }

        return arr[block]->get(id2offset(id));
    }

    void clear()
    {
        for (auto &ele : arr) {
            ele.reset();
        }
    }
};

struct middle_ram_t : public middle_t {
    middle_ram_t();
    virtual ~middle_ram_t();

    void start(const options_t *out_options_) override;
    void stop(osmium::thread::Pool &pool) override;
    void analyze(void) override;
    void end(void) override;
    void commit(void) override;

    void nodes_set(osmium::Node const &node) override;
    size_t nodes_get_list(osmium::WayNodeList *nodes) const override;
    int nodes_delete(osmid_t id);
    int node_changed(osmid_t id);

    void ways_set(osmium::Way const &way) override;
    bool ways_get(osmid_t id, osmium::memory::Buffer &buffer) const override;
    size_t rel_way_members_get(osmium::Relation const &rel, rolelist_t *roles,
                               osmium::memory::Buffer &buffer) const override;

    int ways_delete(osmid_t id);
    int way_changed(osmid_t id);

    bool relations_get(osmid_t id, osmium::memory::Buffer &buffer) const override;
    void relations_set(osmium::Relation const &rel) override;
    int relations_delete(osmid_t id);
    int relation_changed(osmid_t id);

    idlist_t relations_using_way(osmid_t way_id) const override;

    void iterate_ways(middle_t::pending_processor& pf) override;
    void iterate_relations(pending_processor& pf) override;

    size_t pending_count() const override;

    std::shared_ptr<const middle_query_t> get_instance() const override;
private:

    void release_ways();
    void release_relations();

    struct ramWay {
        taglist_t tags;
        idlist_t ndids;

        ramWay(osmium::Way const &way, bool add_attributes)
        : tags(way.tags()), ndids(way.nodes())
        {
            if (add_attributes)
                tags.add_attributes(way);
        }
    };

    struct ramRel {
        taglist_t tags;
        memberlist_t members;

        ramRel(osmium::Relation const &rel, bool add_attributes)
        : tags(rel.tags()), members(rel.members())
        {
            if (add_attributes)
                tags.add_attributes(rel);
        }
    };

    elem_cache_t<ramWay, 10> ways;
    elem_cache_t<ramRel, 10> rels;

    std::unique_ptr<node_ram_cache> cache;

    /* the previous behaviour of iterate_ways was to delete all ways as they
     * were being iterated. this doesn't work now that the output handles its
     * own "done" status and output-specific "pending" status. however, the
     * tests depend on the behaviour that ways will be unavailable once
     * iterate_ways is complete, so this flag emulates that. */
    bool simulate_ways_deleted;
};

#endif
