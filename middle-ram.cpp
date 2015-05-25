/* Implements the mid-layer processing for osm2pgsql
 * using several arrays in RAM. This is fastest if you
 * have sufficient RAM+Swap.
 *
 * This layer stores data read in from the planet.osm file
 * and is then read by the backend processing code to
 * emit the final geometry-enabled output formats
*/

#include <stdexcept>
#include <cstdio>
#include <cstring>
#include <cassert>

#include "middle-ram.hpp"
#include "node-ram-cache.hpp"
#include "options.hpp"
#include "id-tracker.hpp"

/* Object storage now uses 2 levels of storage arrays.
 *
 * - Low level storage of 2^16 (~65k) objects in an indexed array
 *   These are allocated dynamically when we need to first store data with
 *   an ID in this block
 *
 * - Fixed array of 2^(32 - 16) = 65k pointers to the dynamically allocated arrays.
 *
 * This allows memory usage to be efficient and scale dynamically without needing to
 * hard code maximum IDs. We now support an ID  range of -2^31 to +2^31.
 * The negative IDs often occur in non-uploaded JOSM data or other data import scripts.
 *
 */

#define BLOCK_SHIFT 10
#define PER_BLOCK  (1 << BLOCK_SHIFT)
#define NUM_BLOCKS (1 << (32 - BLOCK_SHIFT))

static osmid_t id2block(osmid_t id)
{
    /* + NUM_BLOCKS/2 allows for negative IDs */
    return (id >> BLOCK_SHIFT) + NUM_BLOCKS/2;
}

static  osmid_t id2offset(osmid_t id)
{
    return id & (PER_BLOCK-1);
}

int middle_ram_t::nodes_set(osmid_t id, double lat, double lon, const taglist_t &tags) {
    return cache->set(id, lat, lon, tags);
}

int middle_ram_t::ways_set(osmid_t id, const idlist_t &nds, const taglist_t &tags)
{
    int block  = id2block(id);
    int offset = id2offset(id);

    if (ways[block].empty()) {
        ways[block].assign(PER_BLOCK, ramWay());
    }

    ways[block][offset].ndids = nds;
    ways[block][offset].tags = tags;

    return 0;
}

int middle_ram_t::relations_set(osmid_t id, const memberlist_t &members, const taglist_t &tags)
{
    int block  = id2block(id);
    int offset = id2offset(id);
    if (rels[block].empty()) {
        rels[block].assign(PER_BLOCK, ramRel());
    }

    rels[block][offset].tags = tags;
    rels[block][offset].members = members;

    return 0;
}

int middle_ram_t::nodes_get_list(nodelist_t &out, const idlist_t nds) const
{
    for (idlist_t::const_iterator it = nds.begin(); it != nds.end(); ++it) {
        osmNode n;
        if (!cache->get(&n, *it))
            out.push_back(n);
    }

    return out.size();
}

void middle_ram_t::iterate_relations(pending_processor& pf)
{
    //TODO: just dont do anything

    //let the outputs enqueue everything they have the non slim middle
    //has nothing of its own to enqueue as it doesnt have pending anything
    pf.enqueue_relations(id_tracker::max());

    //let the threads process the relations
    pf.process_relations();
}

size_t middle_ram_t::pending_count() const
{
    return 0;
}

void middle_ram_t::iterate_ways(middle_t::pending_processor& pf)
{
    //let the outputs enqueue everything they have the non slim middle
    //has nothing of its own to enqueue as it doesnt have pending anything
    pf.enqueue_ways(id_tracker::max());

    //let the threads process the ways
    pf.process_ways();
}

void middle_ram_t::release_relations()
{
    rels.clear();
}

void middle_ram_t::release_ways()
{
    ways.clear();
}

int middle_ram_t::ways_get(osmid_t id, taglist_t &tags, nodelist_t &nodes) const
{
    if (simulate_ways_deleted)
        return 1;

    int block = id2block(id), offset = id2offset(id);

    if (ways[block].empty() || ways[block][offset].ndids.empty())
        return 1;

    tags = ways[block][offset].tags;

    nodes_get_list(nodes, ways[block][offset].ndids);

    return 0;
}

int middle_ram_t::ways_get_list(const idlist_t &ids, idlist_t &way_ids,
                                multitaglist_t &tags, multinodelist_t &nodes) const
{
    if (ids.empty())
        return 0;

    assert(way_ids.empty());
    tags.assign(ids.size(), taglist_t());
    nodes.assign(ids.size(), nodelist_t());

    size_t count = 0;
    for (idlist_t::const_iterator it = ids.begin(); it != ids.end(); ++it) {
        if (ways_get(*it, tags[count], nodes[count]) == 0) {
            way_ids.push_back(*it);
            count++;
        } else {
            tags[count].clear();
            nodes[count].clear();
        }
    }

    if (count < ids.size()) {
        tags.resize(count);
        nodes.resize(count);
    }

    return count;
}

int middle_ram_t::relations_get(osmid_t id, memberlist_t &members, taglist_t &tags) const
{
    int block = id2block(id), offset = id2offset(id);

    if (rels[block].empty() || rels[block][offset].members.empty())
        return 1;

    members = rels[block][offset].members;
    tags = rels[block][offset].tags;

    return 0;
}

void middle_ram_t::analyze(void)
{
    /* No need */
}

void middle_ram_t::end(void)
{
    /* No need */
}

int middle_ram_t::start(const options_t *out_options_)
{
    out_options = out_options_;
    /* latlong has a range of +-180, mercator +-20000
       The fixed poing scaling needs adjusting accordingly to
       be stored accurately in an int */
    cache.reset(new node_ram_cache(out_options->alloc_chunkwise, out_options->cache, out_options->scale));

    fprintf( stderr, "Mid: Ram, scale=%d\n", out_options->scale );

    return 0;
}

void middle_ram_t::stop(void)
{
    cache.reset(NULL);

    release_ways();
    release_relations();
}

void middle_ram_t::commit(void) {
}

middle_ram_t::middle_ram_t():
    ways(), rels(), cache(),
    simulate_ways_deleted(false)
{
    ways.resize(NUM_BLOCKS); memset(&ways[0], 0, NUM_BLOCKS * sizeof ways[0]);
    rels.resize(NUM_BLOCKS); memset(&rels[0], 0, NUM_BLOCKS * sizeof rels[0]);
}

middle_ram_t::~middle_ram_t() {
    //instance.reset();
}

std::vector<osmid_t> middle_ram_t::relations_using_way(osmid_t way_id) const
{
    // this function shouldn't be called - relations_using_way is only used in
    // slim mode, and a middle_ram_t shouldn't be constructed if the slim mode
    // option is set.
    throw std::runtime_error("middle_ram_t::relations_using_way is unimlpemented, and "
                             "should not have been called. This is probably a bug, please "
                             "report it at https://github.com/openstreetmap/osm2pgsql/issues");
}

namespace {

void no_delete(const middle_ram_t * middle) {
    // boost::shared_ptr thinks we are going to delete
    // the middle object, but we are not. Heh heh heh.
    // So yeah, this is a hack...
}

}

boost::shared_ptr<const middle_query_t> middle_ram_t::get_instance() const {
    //shallow copy here because readonly access is thread safe
    return boost::shared_ptr<const middle_query_t>(this, no_delete);
}
