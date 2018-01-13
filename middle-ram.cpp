/* Implements the mid-layer processing for osm2pgsql
 * using several arrays in RAM. This is fastest if you
 * have sufficient RAM+Swap.
 *
 * This layer stores data read in from the planet.osm file
 * and is then read by the backend processing code to
 * emit the final geometry-enabled output formats
*/

#include <stdexcept>

#include <cassert>
#include <cstdio>

#include <osmium/builder/attr.hpp>

#include "id-tracker.hpp"
#include "middle-ram.hpp"
#include "node-ram-cache.hpp"
#include "options.hpp"

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

void middle_ram_t::nodes_set(osmium::Node const &node)
{
    cache->set(node.id(), node.location());
}

void middle_ram_t::ways_set(osmium::Way const &way)
{
    ways.set(way.id(), new ramWay(way, out_options->extra_attributes));
}

void middle_ram_t::relations_set(osmium::Relation const &rel)
{
    rels.set(rel.id(), new ramRel(rel, out_options->extra_attributes));
}

size_t middle_ram_t::nodes_get_list(osmium::WayNodeList *nodes) const
{
    size_t count = 0;

    for (auto &n : *nodes) {
        auto loc = cache->get(n.ref());
        n.set_location(loc);
        if (loc.valid()) {
            ++count;
        }
    }

    return count;
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

bool middle_ram_t::ways_get(osmid_t id, osmium::memory::Buffer &buffer) const
{
    if (simulate_ways_deleted) {
        return false;
    }

    auto const *ele = ways.get(id);

    if (!ele) {
        return false;
    }

    using namespace osmium::builder::attr;
    osmium::builder::add_way(buffer, _id(id), _tags(ele->tags), _nodes(ele->ndids));

    return true;
}

size_t middle_ram_t::rel_way_members_get(osmium::Relation const &rel,
                                         rolelist_t *roles,
                                         osmium::memory::Buffer &buffer) const
{
    size_t count = 0;
    for (auto const &m : rel.members()) {
        if (m.type() == osmium::item_type::way && ways_get(m.ref(), buffer)) {
            if (roles) {
                roles->emplace_back(m.role());
            }
            ++count;
        }
    }

    return count;
}

bool middle_ram_t::relations_get(osmid_t id, osmium::memory::Buffer &buffer) const
{
    auto const *ele = rels.get(id);

    if (!ele) {
        return false;
    }

    using namespace osmium::builder::attr;
    osmium::builder::add_relation(buffer, _id(id), _members(ele->members.for_builder()),
                                  _tags(ele->tags));

    return true;
}

void middle_ram_t::analyze(void)
{
    /* No need */
}

void middle_ram_t::end(void)
{
    /* No need */
}

void middle_ram_t::start(const options_t *out_options_)
{
    out_options = out_options_;
    cache.reset(
        new node_ram_cache(out_options->alloc_chunkwise, out_options->cache));
}

void middle_ram_t::stop(osmium::thread::Pool &pool)
{
    cache.reset(nullptr);

    release_ways();
    release_relations();
}

void middle_ram_t::commit(void) {
}

middle_ram_t::middle_ram_t():
    ways(), rels(), cache(), simulate_ways_deleted(false)
{
}

middle_ram_t::~middle_ram_t() {
    //instance.reset();
}

idlist_t middle_ram_t::relations_using_way(osmid_t) const
{
    // this function shouldn't be called - relations_using_way is only used in
    // slim mode, and a middle_ram_t shouldn't be constructed if the slim mode
    // option is set.
    throw std::runtime_error(
        "middle_ram_t::relations_using_way is unimlpemented, and "
        "should not have been called. This is probably a bug, please "
        "report it at https://github.com/openstreetmap/osm2pgsql/issues");
}

namespace {

void no_delete(const middle_ram_t *)
{
    // boost::shared_ptr thinks we are going to delete
    // the middle object, but we are not. Heh heh heh.
    // So yeah, this is a hack...
}

}

std::shared_ptr<const middle_query_t> middle_ram_t::get_instance() const {
    //shallow copy here because readonly access is thread safe
    return std::shared_ptr<const middle_query_t>(this, no_delete);
}
