#include <iostream>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <cassert>
#include <list>
#include <tuple>

#include "osmtypes.hpp"
#include "tests/middle-tests.hpp"

#include <osmium/builder/attr.hpp>
#include <osmium/memory/buffer.hpp>

#define BLOCK_SHIFT 13
#define PER_BLOCK  (((osmid_t)1) << BLOCK_SHIFT)

// simple osmium buffer to store all the objects in
osmium::memory::Buffer buffer(4096, osmium::memory::Buffer::auto_grow::yes);

#define ALLOWED_ERROR 10e-9
bool node_okay(osmNode node, osmium::Node const &expected) {
    if ((node.lat > expected.location().lat() + ALLOWED_ERROR)
            || (node.lat < expected.location().lat() - ALLOWED_ERROR)) {
        std::cerr << "ERROR: Node should have lat=" << expected.location().lat() << ", but got back "
            << node.lat << " from middle.\n";
        return false;
    }
    if ((node.lon > expected.location().lon() + ALLOWED_ERROR)
            || (node.lon < expected.location().lon() - ALLOWED_ERROR)) {
        std::cerr << "ERROR: Node should have lon=" << expected.location().lon() << ", but got back "
            << node.lon << " from middle.\n";
        return false;
    }
    return true;
}

size_t add_node(osmid_t id, double lat, double lon)
{
    using namespace osmium::builder::attr;
    return osmium::builder::add_node(buffer, _id(id), _location(lon, lat));
}

size_t way_with_nodes(std::vector<osmid_t> const &ids)
{
    using namespace osmium::builder::attr;
    return osmium::builder::add_way(buffer, _nodes(ids));
}

int test_node_set(middle_t *mid)
{
    buffer.clear();

    auto const &node = buffer.get<osmium::Node>(add_node(1234, 12.3456789, 98.7654321));
    auto const &way = buffer.get<osmium::Way>(way_with_nodes({node.id()}));
    nodelist_t nodes;

    // set the node
    mid->nodes_set(node, node.location().lat(), node.location().lon(), false);

    // get it back
    if (mid->nodes_get_list(nodes, way.nodes()) != way.nodes().size()) {
        std::cerr << "ERROR: Unable to get node list.\n";
        return 1;
    }
    if (nodes.size() != way.nodes().size()) {
        std::cerr << "ERROR: Mismatch in returned node list size.\n";
        return 1;
    }

    // check that it's the same
    if (!node_okay(nodes[0], node)) {
        return 1;
    }

    return 0;
}

inline double test_lat(osmid_t id) {
    return 1 + 1e-5 * id;
}

int test_nodes_comprehensive_set(middle_t *mid)
{
    std::vector<size_t> expected_nodes;
    expected_nodes.reserve(PER_BLOCK*8+1);

    buffer.clear();

    // 2 dense blocks, the second partially filled at the star
    for (osmid_t id = 0; id < (PER_BLOCK+(PER_BLOCK >> 1) + 1); ++id)
    {
        expected_nodes.emplace_back(add_node(id, test_lat(id), 0.0));
    }

    // 1 dense block, 75% filled
    for (osmid_t id = PER_BLOCK*2; id < PER_BLOCK*3; ++id)
    {
        if ((id % 4 == 0) || (id % 4 == 1) || (id % 4 == 2))
            expected_nodes.emplace_back(add_node(id, test_lat(id), 0.0));
    }

    // 1 dense block, sparsly filled
    for (osmid_t id = PER_BLOCK*3; id < PER_BLOCK*4; ++id)
    {
        if (id % 4 == 0)
            expected_nodes.emplace_back(add_node(id, test_lat(id), 0.0));
    }

    // A lone sparse node
    expected_nodes.emplace_back(add_node(PER_BLOCK*5, test_lat(PER_BLOCK*5), 0.0));

    // A dense block of alternating positions of zero/non-zero
    for (osmid_t id = PER_BLOCK*6; id < PER_BLOCK*7; ++id)
    {
        if (id % 2 == 0)
            expected_nodes.emplace_back(add_node(id, 0.0, 0.0));
        else
            expected_nodes.emplace_back(add_node(id, test_lat(id), 0.0));
    }
    expected_nodes.emplace_back(add_node(PER_BLOCK*8, 0.0, 0.0));
    expected_nodes.emplace_back(add_node(PER_BLOCK*8+1, 0.0, 0.0));

    // Load up the nodes into the middle
    std::vector<osmid_t> ids;

    for (auto pos : expected_nodes)
    {
        auto const &node = buffer.get<osmium::Node>(pos);
        mid->nodes_set(node, node.location().lat(), node.location().lon(), false);
        ids.push_back(node.id());
    }

    auto const &way = buffer.get<osmium::Way>(way_with_nodes(ids));

    nodelist_t nodes;
    if (mid->nodes_get_list(nodes, way.nodes()) != ids.size()) {
        std::cerr << "ERROR: Unable to get node list.\n";
        return 1;
    }

    if (nodes.size() != ids.size()) {
        std::cerr << "ERROR: Mismatch in returned node list size.\n";
        return 1;
    }

    for (size_t i = 0; i < nodes.size(); ++i)
    {
        auto const &node = buffer.get<osmium::Node>(expected_nodes[i]);
        if (!node_okay(nodes[i], node)) {
            return 1;
        }
    }

    return 0;
}

struct test_pending_processor : public middle_t::pending_processor {
    test_pending_processor(): pending_ways(), pending_rels() {}
    ~test_pending_processor() {}
    void enqueue_ways(osmid_t id) override
    {
        pending_ways.push_back(id);
    }
    void process_ways() override
    {
        pending_ways.clear();
    }
    void enqueue_relations(osmid_t id) override
    {
        pending_rels.push_back(id);
    }
    void process_relations() override
    {
        pending_rels.clear();
    }
    std::list<osmid_t> pending_ways;
    std::list<osmid_t> pending_rels;
};

int test_way_set(middle_t *mid)
{
    buffer.clear();

    osmid_t way_id = 1;
    double lat = 12.3456789;
    double lon = 98.7654321;
    osmNode *node_ptr = nullptr;
    idlist_t nds;
    std::vector<size_t> nodes;

    // set nodes
    for (osmid_t i = 1; i <= 10; ++i) {
        nds.push_back(i);
        nodes.push_back(add_node(i, lat, lon));
        auto const &node = buffer.get<osmium::Node>(nodes.back());
        mid->nodes_set(node, lat, lon, false);
    }

    // set the way
    {
        using namespace osmium::builder::attr;
        auto pos = osmium::builder::add_way(buffer, _id(way_id), _nodes(nds));
        auto const &way = buffer.get<osmium::Way>(pos);
        mid->ways_set(way, false);
    }

    // commit the setup data
    mid->commit();

    // get it back
    idlist_t ways;
    ways.push_back(way_id);
    auto buf_pos = buffer.committed();
    size_t way_count = mid->ways_get_list(ways, buffer);
    if (way_count != 1) { std::cerr << "ERROR: Unable to get way list.\n"; return 1; }

    auto const &way = buffer.get<osmium::Way>(buf_pos);
    // check that it's the same
    if (way.nodes().size() != nds.size()) {
        std::cerr << "ERROR: Way should have " << nds.size() << " nodes, but got back "
            << way.nodes().size() << " from middle.\n";
        return 1;
    }
    if (way.id() != way_id) {
        std::cerr << "ERROR: Way should have id=" << way_id << ", but got back "
            << way.id() << " from middle.\n";
        return 1;
    }
    nodelist_t xnodes;
    mid->nodes_get_list(xnodes, way.nodes());
    for (size_t i = 0; i < nds.size(); ++i) {
        if (xnodes[i].lon != lon) {
            std::cerr << "ERROR: Way node should have lon=" << lon << ", but got back "
                << node_ptr[i].lon << " from middle.\n";
            return 1;
        }
        if (xnodes[i].lat != lat) {
            std::cerr << "ERROR: Way node should have lat=" << lat << ", but got back "
                << node_ptr[i].lat << " from middle.\n";
            return 1;
        }
    }

    // the way we just inserted should not be pending
    test_pending_processor tpp;
    mid->iterate_ways(tpp);
    if (mid->pending_count() != 0) {
        std::cerr << "ERROR: Was expecting no pending ways, but got "
            << mid->pending_count() << " from middle.\n";
        return 1;
    }

    // some middles don't support changing the nodes - they
    // don't have diff update ability. here, we will just
    // skip the test for that.
    if (dynamic_cast<slim_middle_t *>(mid)) {
        slim_middle_t *slim = dynamic_cast<slim_middle_t *>(mid);

        // finally, try touching a node on a non-pending way. that should
        // make it become pending. we just checked that the way is not
        // pending, so any change must be due to the node changing.
        slim->node_changed(nds[0]);
        slim->iterate_ways(tpp);
        if (slim->pending_count() != 1) {
            std::cerr << "ERROR: Was expecting a single pending way from node update, but got "
                << slim->pending_count() << " from middle.\n";
            return 1;
        }
    }

    return 0;
}
