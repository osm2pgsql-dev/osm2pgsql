/* Implements the mid-layer processing for osm2pgsql
 * using several arrays in RAM. This is fastest if you
 * have sufficient RAM+Swap.
 *
 * This layer stores data read in from the planet.osm file
 * and is then read by the backend processing code to
 * emit the final geometry-enabled output formats
*/

#include <cassert>
#include <memory>

#include <osmium/builder/attr.hpp>

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

void middle_ram_t::node_set(osmium::Node const &node)
{
    m_cache->set(node.id(), node.location());
}

void middle_ram_t::way_set(osmium::Way const &way)
{
    m_ways.set(way.id(), new ramWay{way, m_extra_attributes});
}

void middle_ram_t::relation_set(osmium::Relation const &rel)
{
    m_rels.set(rel.id(), new ramRel{rel, m_extra_attributes});
}

size_t middle_ram_t::nodes_get_list(osmium::WayNodeList *nodes) const
{
    assert(nodes);
    size_t count = 0;

    for (auto &n : *nodes) {
        auto loc = m_cache->get(n.ref());
        n.set_location(loc);
        if (loc.valid()) {
            ++count;
        }
    }

    return count;
}

bool middle_ram_t::way_get(osmid_t id, osmium::memory::Buffer &buffer) const
{
    auto const *ele = m_ways.get(id);

    if (!ele) {
        return false;
    }

    using namespace osmium::builder::attr;
    osmium::builder::add_way(buffer, _id(id), _tags(ele->tags),
                             _nodes(ele->ndids));

    return true;
}

size_t middle_ram_t::rel_way_members_get(osmium::Relation const &rel,
                                         rolelist_t *roles,
                                         osmium::memory::Buffer &buffer) const
{
    size_t count = 0;
    for (auto const &m : rel.members()) {
        if (m.type() == osmium::item_type::way && way_get(m.ref(), buffer)) {
            if (roles) {
                roles->emplace_back(m.role());
            }
            ++count;
        }
    }

    return count;
}

bool middle_ram_t::relation_get(osmid_t id,
                                osmium::memory::Buffer &buffer) const
{
    auto const *ele = m_rels.get(id);

    if (!ele) {
        return false;
    }

    using namespace osmium::builder::attr;
    osmium::builder::add_relation(buffer, _id(id),
                                  _members(ele->members.for_builder()),
                                  _tags(ele->tags));

    return true;
}

void middle_ram_t::stop(thread_pool_t &)
{
    m_cache.reset();
    m_ways.clear();
    m_rels.clear();
}

middle_ram_t::middle_ram_t(options_t const *options)
: m_cache(new node_ram_cache{options->alloc_chunkwise, options->cache}),
  m_extra_attributes(options->extra_attributes)
{}

std::shared_ptr<middle_query_t> middle_ram_t::get_query_instance()
{
    return shared_from_this();
}
