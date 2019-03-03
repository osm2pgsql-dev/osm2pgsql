#ifndef TESTS_MIDDLE_TEST_HPP
#define TESTS_MIDDLE_TEST_HPP

#include <memory>
#include <vector>

#include "middle-pgsql.hpp"
#include "middle-ram.hpp"
#include "options.hpp"
#include "output-null.hpp"
#include "reprojection.hpp"

#include <osmium/builder/attr.hpp>
#include <osmium/memory/buffer.hpp>

class test_middle_helper
{
    enum
    {
        BLOCK_SHIFT = 13,
        PER_BLOCK = (1ULL << BLOCK_SHIFT)
    };

    struct test_pending_processor : public middle_t::pending_processor
    {
        void enqueue_ways(osmid_t id) override { pending_ways.push_back(id); }
        void process_ways() override { pending_ways.clear(); }
        void enqueue_relations(osmid_t id) override
        {
            pending_rels.push_back(id);
        }
        void process_relations() override { pending_rels.clear(); }
        std::vector<osmid_t> pending_ways;
        std::vector<osmid_t> pending_rels;
    };

public:
    test_middle_helper(options_t const &options)
    : m_buffer(4096, osmium::memory::Buffer::auto_grow::yes),
      m_proj(reprojection::create_projection(PROJ_LATLONG))
    {
        if (options.slim) {
            m_mid = std::shared_ptr<middle_t>(new middle_pgsql_t(&options));
        } else {
            m_mid = std::shared_ptr<middle_t>(new middle_ram_t(&options));
        }
        m_mid->start();
        m_mid_q = m_mid->get_query_instance(m_mid);
        m_output.reset(new output_null_t(m_mid_q, options));
    }

    ~test_middle_helper()
    {
        m_mid->commit();
    }

    // tests that a single node can be set and retrieved. returns 0 on success.
    int test_node_set()
    {
        m_buffer.clear();

        auto const &node =
            m_buffer.get<osmium::Node>(add_node(1234, 12.3456789, 98.7654321));
        auto &way = m_buffer.get<osmium::Way>(way_with_nodes({node.id()}));

        // set the node
        m_mid->nodes_set(node);
        m_mid->flush(osmium::item_type::way);

        // get it back
        if (m_mid_q->nodes_get_list(&(way.nodes())) != way.nodes().size()) {
            std::cerr << "ERROR: Unable to get node list.\n";
            return 1;
        }

        // check that it's the same
        if (!node_okay(way.nodes()[0].location(), node)) {
            return 1;
        }

        return 0;
    }

    // tests various combinations of nodes being set and retrieved to trigger different cache strategies. returns 0 on success.
    int test_nodes_comprehensive_set()
    {
        std::vector<size_t> expected_nodes;
        expected_nodes.reserve(PER_BLOCK * 8 + 1);

        m_buffer.clear();

        // 2 dense blocks, the second partially filled at the star
        for (osmid_t id = 0; id < (PER_BLOCK + (PER_BLOCK >> 1) + 1); ++id) {
            expected_nodes.emplace_back(add_node(id, test_lat(id), 0.0));
        }

        // 1 dense block, 75% filled
        for (osmid_t id = PER_BLOCK * 2; id < PER_BLOCK * 3; ++id) {
            if ((id % 4 == 0) || (id % 4 == 1) || (id % 4 == 2))
                expected_nodes.emplace_back(add_node(id, test_lat(id), 0.0));
        }

        // 1 dense block, sparsly filled
        for (osmid_t id = PER_BLOCK * 3; id < PER_BLOCK * 4; ++id) {
            if (id % 4 == 0)
                expected_nodes.emplace_back(add_node(id, test_lat(id), 0.0));
        }

        // A lone sparse node
        expected_nodes.emplace_back(
            add_node(PER_BLOCK * 5, test_lat(PER_BLOCK * 5), 0.0));

        // A dense block of alternating positions of zero/non-zero
        for (osmid_t id = PER_BLOCK * 6; id < PER_BLOCK * 7; ++id) {
            if (id % 2 == 0)
                expected_nodes.emplace_back(add_node(id, 0.0, 0.0));
            else
                expected_nodes.emplace_back(add_node(id, test_lat(id), 0.0));
        }
        expected_nodes.emplace_back(add_node(PER_BLOCK * 8, 0.0, 0.0));
        expected_nodes.emplace_back(add_node(PER_BLOCK * 8 + 1, 0.0, 0.0));

        // Load up the nodes into the middle
        std::vector<osmid_t> ids;

        for (auto pos : expected_nodes) {
            auto const &node = m_buffer.get<osmium::Node>(pos);
            m_mid->nodes_set(node);
            ids.push_back(node.id());
        }

        m_mid->flush(osmium::item_type::way);

        auto &way = m_buffer.get<osmium::Way>(way_with_nodes(ids));

        if (m_mid_q->nodes_get_list(&(way.nodes())) != ids.size()) {
            std::cerr << "ERROR: Unable to get node list.\n";
            return 1;
        }

        for (size_t i = 0; i < ids.size(); ++i) {
            auto const &node = m_buffer.get<osmium::Node>(expected_nodes[i]);
            if (!node_okay(way.nodes()[i].location(), node)) {
                return 1;
            }
        }

        return 0;
    }

    // tests that a single way and supporting nodes can be set and retrieved.
    // returns 0 on success.
    int test_way_set()
    {
        m_buffer.clear();

        osmid_t way_id = 1;
        double lat = 12.3456789;
        double lon = 98.7654321;
        idlist_t nds;
        std::vector<size_t> nodes;

        // set nodes
        for (osmid_t i = 1; i <= 10; ++i) {
            nds.push_back(i);
            nodes.push_back(add_node(i, lat, lon));
            auto const &node = m_buffer.get<osmium::Node>(nodes.back());
            m_mid->nodes_set(node);
        }
        m_mid->flush(osmium::item_type::node);

        // set the way
        {
            using namespace osmium::builder::attr;
            auto pos =
                osmium::builder::add_way(m_buffer, _id(way_id), _nodes(nds));
            auto const &way = m_buffer.get<osmium::Way>(pos);
            m_mid->ways_set(way);
        }

        m_mid->flush(osmium::item_type::way);

        // get it back
        osmium::memory::Buffer relbuf(4096,
                                      osmium::memory::Buffer::auto_grow::yes);
        {
            using namespace osmium::builder::attr;
            osmium::builder::add_relation(
                relbuf, _id(123), _member(osmium::item_type::node, 132),
                _member(osmium::item_type::way, way_id, "outer"));
        }

        auto const &rel = relbuf.get<osmium::Relation>(0);

        auto buf_pos = m_buffer.committed();
        rolelist_t roles;
        size_t way_count = m_mid_q->rel_way_members_get(rel, &roles, m_buffer);
        if (way_count != 1) {
            std::cerr << "ERROR: Unable to get way list.\n";
            return 1;
        }

        if (roles.size() != 1) {
            std::cerr << "Bad length of role ist. Expected 1, got "
                      << roles.size() << ".\n";
            return 1;
        }

        if (strcmp(roles[0], "outer") != 0) {
            std::cerr << "Bad role. Expected 'outer', got '" << roles[0]
                      << "'.\n";
            return 1;
        }

        auto &way = m_buffer.get<osmium::Way>(buf_pos);
        // check that it's the same
        if (way.nodes().size() != nds.size()) {
            std::cerr << "ERROR: Way should have " << nds.size()
                      << " nodes, but got back " << way.nodes().size()
                      << " from middle.\n";
            return 1;
        }
        if (way.id() != way_id) {
            std::cerr << "ERROR: Way should have id=" << way_id
                      << ", but got back " << way.id() << " from middle.\n";
            return 1;
        }
        m_mid_q->nodes_get_list(&(way.nodes()));
        for (size_t i = 0; i < nds.size(); ++i) {
            if (way.nodes()[i].location().lon() != lon) {
                std::cerr << "ERROR: Way node should have lon=" << lon
                          << ", but got back "
                          << way.nodes()[i].location().lon()
                          << " from middle.\n";
                return 1;
            }
            if (way.nodes()[i].location().lat() != lat) {
                std::cerr << "ERROR: Way node should have lat=" << lat
                          << ", but got back "
                          << way.nodes()[i].location().lat()
                          << " from middle.\n";
                return 1;
            }
        }

        // the way we just inserted should not be pending
        test_pending_processor tpp;
        m_mid->iterate_ways(tpp);
        if (m_mid->pending_count() != 0) {
            std::cerr << "ERROR: Was expecting no pending ways, but got "
                      << m_mid->pending_count() << " from middle.\n";
            return 1;
        }

        // some middles don't support changing the nodes - they
        // don't have diff update ability. here, we will just
        // skip the test for that.
        if (dynamic_cast<slim_middle_t *>(m_mid.get())) {
            slim_middle_t *slim = dynamic_cast<slim_middle_t *>(m_mid.get());

            // finally, try touching a node on a non-pending way. that should
            // make it become pending. we just checked that the way is not
            // pending, so any change must be due to the node changing.
            slim->node_changed(nds[0]);
            slim->iterate_ways(tpp);
            if (slim->pending_count() != 1) {
                std::cerr << "ERROR: Was expecting a single pending way from "
                             "node update, but got "
                          << slim->pending_count() << " from middle.\n";
                return 1;
            }
        }

        return 0;
    }

private:
    static constexpr double allowed_error() { return 10e-9; }

    static bool node_okay(osmium::Location loc, osmium::Node const &expected)
    {
        if ((loc.lat() > expected.location().lat() + allowed_error()) ||
            (loc.lat() < expected.location().lat() - allowed_error())) {
            std::cerr << "ERROR: Node should have lat="
                      << expected.location().lat() << ", but got back "
                      << loc.lat() << " from middle.\n";
            return false;
        }
        if ((loc.lon() > expected.location().lon() + allowed_error()) ||
            (loc.lon() < expected.location().lon() - allowed_error())) {
            std::cerr << "ERROR: Node should have lon="
                      << expected.location().lon() << ", but got back "
                      << loc.lon() << " from middle.\n";
            return false;
        }
        return true;
    }

    size_t add_node(osmid_t id, double lat, double lon)
    {
        using namespace osmium::builder::attr;
        return osmium::builder::add_node(m_buffer, _id(id),
                                         _location(lon, lat));
    }

    size_t way_with_nodes(std::vector<osmid_t> const &ids)
    {
        using namespace osmium::builder::attr;
        return osmium::builder::add_way(m_buffer, _nodes(ids));
    }

    static constexpr double test_lat(osmid_t id) { return 1 + 1e-5 * id; }

    std::shared_ptr<middle_t> m_mid;
    std::shared_ptr<middle_query_t> m_mid_q;
    std::unique_ptr<output_null_t> m_output;

    // simple osmium buffer to store all the objects in
    osmium::memory::Buffer m_buffer;
    // 1:1 projection
    std::shared_ptr<reprojection> m_proj;
};

#endif /* TESTS_MIDDLE_TEST_HPP */
