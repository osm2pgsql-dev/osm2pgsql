#include <catch.hpp>

#include <osmium/osm/crc.hpp>
#include <osmium/osm/crc_zlib.hpp>

#include "middle-pgsql.hpp"
#include "middle-ram.hpp"

#include "common-cleanup.hpp"
#include "common-options.hpp"
#include "common-pg.hpp"

static pg::tempdb_t db;

namespace {

/// Simple osmium buffer to store object with some convenience.
class test_buffer_t
{
public:
    size_t add_node(osmid_t id, double lon, double lat)
    {
        using namespace osmium::builder::attr;
        return osmium::builder::add_node(buf, _id(id), _location(lon, lat));
    }

    size_t add_way(osmid_t wid, idlist_t const &ids)
    {
        using namespace osmium::builder::attr;
        return osmium::builder::add_way(buf, _id(wid), _nodes(ids));
    }

    size_t add_relation(
        osmid_t rid,
        std::initializer_list<osmium::builder::attr::member_type> members)
    {
        using namespace osmium::builder::attr;
        return osmium::builder::add_relation(
            buf, _id(rid), _members(members.begin(), members.end()));
    }

    size_t add_nodes(idlist_t const &ids)
    {
        using namespace osmium::builder::attr;
        return osmium::builder::add_way_node_list(buf, _nodes(ids));
    }

    template <typename T>
    T const &get(size_t pos) const
    {
        return buf.get<T>(pos);
    }

    template <typename T>
    T &get(size_t pos)
    {
        return buf.get<T>(pos);
    }

    osmium::Node const &add_node_and_get(osmid_t id, double lon, double lat)
    {
        return get<osmium::Node>(add_node(id, lon, lat));
    }

    osmium::Way &add_way_and_get(osmid_t wid, idlist_t const &ids)
    {
        return get<osmium::Way>(add_way(wid, ids));
    }

    osmium::Relation &add_relation_and_get(
        osmid_t rid,
        std::initializer_list<osmium::builder::attr::member_type> members)
    {
        return get<osmium::Relation>(add_relation(rid, members));
    }

    osmium::WayNodeList &add_nodes_and_get(idlist_t const &nodes)
    {
        return get<osmium::WayNodeList>(add_nodes(nodes));
    }

private:
    osmium::memory::Buffer buf{4096, osmium::memory::Buffer::auto_grow::yes};
};

void expect_location(osmium::Location loc, osmium::Node const &expected)
{
    CHECK(loc.lat() == Approx(expected.location().lat()));
    CHECK(loc.lon() == Approx(expected.location().lon()));
}
} // namespace

struct options_slim_default
{
    static options_t options(pg::tempdb_t const &tmpdb)
    {
        return testing::opt_t().slim(tmpdb);
    }
};

struct options_slim_dense_cache
{
    static options_t options(pg::tempdb_t const &tmpdb)
    {
        options_t o = options_slim_default::options(tmpdb);
        o.alloc_chunkwise = ALLOC_DENSE;
        return o;
    }
};

struct options_flat_node_cache
{
    static options_t options(pg::tempdb_t const &tmpdb)
    {
        return testing::opt_t().slim(tmpdb).flatnodes();
    }
};

struct options_ram_optimized
{
    static options_t options(pg::tempdb_t const &)
    {
        options_t o = testing::opt_t();
        o.alloc_chunkwise = ALLOC_SPARSE | ALLOC_DENSE;
        return o;
    }
};

struct options_ram_flatnode
{
    static options_t options(pg::tempdb_t const &)
    {
        options_t o = testing::opt_t().flatnodes();
        o.alloc_chunkwise = ALLOC_SPARSE | ALLOC_DENSE;
        return o;
    }
};

TEST_CASE("elem_cache_t")
{
    elem_cache_t<int, 10> cache;

    cache.set(3, new int{23});
    cache.set(5, new int{42});
    REQUIRE(*cache.get(3) == 23);
    REQUIRE(*cache.get(5) == 42);
    REQUIRE(cache.get(2) == nullptr);
    cache.set(2, new int{56});
    REQUIRE(*cache.get(2) == 56);
    cache.set(3, new int{0});
    REQUIRE(*cache.get(3) == 0);
    cache.clear();
    REQUIRE(cache.get(1) == nullptr);
    REQUIRE(cache.get(2) == nullptr);
    REQUIRE(cache.get(3) == nullptr);
}

TEMPLATE_TEST_CASE("middle import", "", options_slim_default,
                   options_slim_dense_cache, options_ram_optimized,
                   options_ram_flatnode)
{
    options_t const options = TestType::options(db);
    testing::cleanup::file_t flatnode_cleaner{
        options.flat_node_file.get_value_or("")};

    auto mid = options.slim
                   ? std::shared_ptr<middle_t>(new middle_pgsql_t{&options})
                   : std::shared_ptr<middle_t>(new middle_ram_t{&options});

    mid->start();

    auto const mid_q = mid->get_query_instance();

    test_buffer_t buffer;

    SECTION("Set and retrieve a single node")
    {
        auto const &node =
            buffer.add_node_and_get(1234, 98.7654321, 12.3456789);

        // set the node
        mid->nodes_set(node);
        mid->flush();

        // getting it back works only via a waylist
        auto &nodes = buffer.add_way_and_get(3, {1234}).nodes();

        // get it back
        REQUIRE(mid_q->nodes_get_list(&nodes) == nodes.size());
        expect_location(nodes[0].location(), node);

        // other nodes are not retrievable
        auto &n2 = buffer.add_way_and_get(3, {1, 2, 1235}).nodes();
        REQUIRE(mid_q->nodes_get_list(&n2) == 0);
    }

    SECTION("Set and retrieve a single way")
    {
        osmid_t const way_id = 1;
        double const lon = 98.7654321;
        double const lat = 12.3456789;
        idlist_t nds;
        std::vector<size_t> nodes;

        // set nodes
        for (osmid_t i = 1; i <= 10; ++i) {
            nds.push_back(i);
            nodes.push_back(
                buffer.add_node(i, lon - i * 0.003, lat + i * 0.001));
            mid->nodes_set(buffer.get<osmium::Node>(nodes.back()));
        }

        // set the way
        mid->ways_set(buffer.add_way_and_get(way_id, nds));

        mid->flush();

        // get it back
        osmium::memory::Buffer outbuf{4096,
                                      osmium::memory::Buffer::auto_grow::yes};

        REQUIRE(mid_q->ways_get(way_id, outbuf));

        auto &way = outbuf.get<osmium::Way>(0);

        CHECK(way.id() == way_id);
        REQUIRE(way.nodes().size() == nds.size());

        REQUIRE(mid_q->nodes_get_list(&(way.nodes())) == nds.size());
        for (osmid_t i = 1; i <= 10; ++i) {
            CHECK(way.nodes()[(size_t)i - 1].ref() == i);
        }

        // other ways are not retrievable
        REQUIRE_FALSE(mid_q->ways_get(way_id + 1, outbuf));
    }

    SECTION("Set and retrieve a single relation with supporting ways")
    {
        idlist_t const nds[] = {{4, 5, 13, 14, 342}, {45, 90}, {30, 3, 45}};

        // set the node
        mid->nodes_set(buffer.add_node_and_get(1, 4.1, 12.8));

        // set the ways
        osmid_t wid = 10;
        for (auto const &n : nds) {
            mid->ways_set(buffer.add_way_and_get(wid, n));
            ++wid;
        }

        // set the relation
        using otype = osmium::item_type;
        auto const &relation =
            buffer.add_relation_and_get(123, {{otype::way, 11, ""},
                                              {otype::way, 10, "outer"},
                                              {otype::node, 1},
                                              {otype::way, 12, "inner"}});
        osmium::CRC<osmium::CRC_zlib> orig_crc;
        orig_crc.update(relation);

        mid->relations_set(relation);

        mid->flush();

        // retrieve the relation
        osmium::memory::Buffer outbuf{4096,
                                      osmium::memory::Buffer::auto_grow::yes};
        REQUIRE(mid_q->relations_get(123, outbuf));
        auto const &rel = outbuf.get<osmium::Relation>(0);

        CHECK(rel.id() == 123);
        CHECK(rel.members().size() == 4);

        osmium::CRC<osmium::CRC_zlib> crc;
        crc.update(rel);
        CHECK(orig_crc().checksum() == crc().checksum());

        // retrive the supporting ways
        rolelist_t roles;
        REQUIRE(mid_q->rel_way_members_get(rel, &roles, outbuf) == 3);
        REQUIRE(roles.size() == 3);

        for (auto &w : outbuf.select<osmium::Way>()) {
            REQUIRE(w.id() >= 10);
            REQUIRE(w.id() <= 12);
            auto const &expected = nds[w.id() - 10];
            REQUIRE(w.nodes().size() == expected.size());
            for (size_t i = 0; i < expected.size(); ++i) {
                REQUIRE(w.nodes()[i].ref() == expected[i]);
            }
        }

        // other relations are not retrievable
        REQUIRE_FALSE(mid_q->relations_get(999, outbuf));
    }
}

/**
 * Check that the node is in the mid with the right id and location
 */
static void check_node(std::shared_ptr<middle_pgsql_t> const &mid,
                       osmium::Node const &node)
{
    test_buffer_t buffer;
    auto &nodes = buffer.add_nodes_and_get({node.id()});
    auto const mid_q = mid->get_query_instance();
    REQUIRE(mid_q->nodes_get_list(&nodes) == 1);
    REQUIRE(nodes[0].ref() == node.id());
    REQUIRE(nodes[0].location() == node.location());
}

/// Return true if the node with the specified id is not in the mid
static bool no_node(std::shared_ptr<middle_pgsql_t> const &mid, osmid_t id)
{
    test_buffer_t buffer;
    auto &nodes = buffer.add_nodes_and_get({id});
    auto const mid_q = mid->get_query_instance();
    return mid_q->nodes_get_list(&nodes) == 0;
}

TEMPLATE_TEST_CASE("middle add, delete and update node", "",
                   options_slim_default, options_slim_dense_cache,
                   options_flat_node_cache)
{
    options_t options = TestType::options(db);

    testing::cleanup::file_t flatnode_cleaner{
        options.flat_node_file.get_value_or("")};

    // Prepare a buffer with some nodes which we will add and change
    test_buffer_t buffer;
    auto const &node10 = buffer.add_node_and_get(10, 1.0, 0.0);
    auto const &node11 = buffer.add_node_and_get(11, 1.1, 0.0);
    auto const &node12 = buffer.add_node_and_get(12, 1.2, 0.0);

    auto const &node10a = buffer.add_node_and_get(10, 1.0, 1.0);

    // Set up middle in "create" mode to get a cleanly initialized database
    // and add some nodes. Does this in its own scope so that the mid is
    // closed up properly.
    {
        auto mid = std::make_shared<middle_pgsql_t>(&options);
        mid->start();

        mid->nodes_set(node10);
        mid->nodes_set(node11);
        mid->flush();

        check_node(mid, node10);
        check_node(mid, node11);
        mid->commit();
    }

    // From now on use append mode to not destroy the data we just added
    options.append = true;

    SECTION("Check that added nodes are there and no others")
    {
        auto mid = std::make_shared<middle_pgsql_t>(&options);
        mid->start();

        check_node(mid, node10);
        check_node(mid, node11);
        REQUIRE(no_node(mid, 5));
        REQUIRE(no_node(mid, 42));

        mid->commit();
    }

    SECTION("Delete existing and non-existing node")
    {
        {
            auto mid = std::make_shared<middle_pgsql_t>(&options);
            mid->start();

            mid->nodes_delete(5);
            mid->nodes_delete(10);
            mid->nodes_delete(42);
            mid->flush();

            REQUIRE(no_node(mid, 5));
            REQUIRE(no_node(mid, 10));
            check_node(mid, node11);
            REQUIRE(no_node(mid, 42));

            mid->commit();
        }
        {
            // Check with a new mid
            auto mid = std::make_shared<middle_pgsql_t>(&options);
            mid->start();

            REQUIRE(no_node(mid, 5));
            REQUIRE(no_node(mid, 10));
            check_node(mid, node11);
            REQUIRE(no_node(mid, 42));

            mid->commit();
        }
    }

    SECTION("Change (delete and set) existing node and non-existing node")
    {
        {
            auto mid = std::make_shared<middle_pgsql_t>(&options);
            mid->start();

            mid->nodes_delete(10);
            mid->nodes_set(node10a);
            mid->nodes_delete(12);
            mid->nodes_set(node12);
            mid->flush();

            check_node(mid, node10a);
            check_node(mid, node11);
            check_node(mid, node12);

            mid->commit();
        }
        {
            // Check with a new mid
            auto mid = std::make_shared<middle_pgsql_t>(&options);
            mid->start();

            check_node(mid, node10a);
            check_node(mid, node11);
            check_node(mid, node12);

            mid->commit();
        }
    }

    SECTION("Add new node")
    {
        {
            auto mid = std::make_shared<middle_pgsql_t>(&options);
            mid->start();

            mid->nodes_set(node12);
            mid->flush();

            REQUIRE(no_node(mid, 5));
            check_node(mid, node10);
            check_node(mid, node11);
            check_node(mid, node12);
            REQUIRE(no_node(mid, 42));

            mid->commit();
        }
        {
            // Check with a new mid
            auto mid = std::make_shared<middle_pgsql_t>(&options);
            mid->start();

            REQUIRE(no_node(mid, 5));
            check_node(mid, node10);
            check_node(mid, node11);
            check_node(mid, node12);
            REQUIRE(no_node(mid, 42));

            mid->commit();
        }
    }
}

TEMPLATE_TEST_CASE("middle: add, delete and update way", "",
                   options_slim_default, options_slim_dense_cache,
                   options_flat_node_cache)
{
    options_t options = TestType::options(db);

    testing::cleanup::file_t flatnode_cleaner{
        options.flat_node_file.get_value_or("")};

    osmid_t const way_id = 20;

    // create some nodes we'll use for the ways
    test_buffer_t buffer;
    auto const& node10 = buffer.add_node_and_get(10, 1.0, 0.0);
    auto const& node11 = buffer.add_node_and_get(11, 1.1, 0.0);
    auto const& node12 = buffer.add_node_and_get(12, 1.2, 0.0);

    // Set up middle in "create" mode to get a cleanly initialized database
    // and add some nodes.
    {
        auto mid = std::make_shared<middle_pgsql_t>(&options);
        mid->start();

        mid->nodes_set(node10);
        mid->nodes_set(node11);
        mid->nodes_set(node12);
        mid->flush();

        // create way
        idlist_t nds{10, 11};
        mid->ways_set(buffer.add_way_and_get(way_id, nds));
        mid->flush();

        osmium::memory::Buffer outbuf{4096,
                                      osmium::memory::Buffer::auto_grow::yes};

        auto const mid_q = mid->get_query_instance();
        REQUIRE(mid_q->ways_get(way_id, outbuf));

        auto &way = outbuf.get<osmium::Way>(0);

        REQUIRE(way.id() == way_id);
        REQUIRE(way.nodes().size() == nds.size());
        REQUIRE(way.nodes()[0].ref() == 10);
        REQUIRE(way.nodes()[1].ref() == 11);

        REQUIRE(mid_q->nodes_get_list(&(way.nodes())) == nds.size());
        REQUIRE(way.nodes()[0].location() == osmium::Location{1.0, 0.0});
        REQUIRE(way.nodes()[1].location() == osmium::Location{1.1, 0.0});

        mid->commit();
    }

    // Set up middle again, this time in "append" mode so we can change things.
    {
        options.append = true;
        auto mid = std::make_shared<middle_pgsql_t>(&options);
        mid->start();

        // delete way
        mid->ways_delete(way_id);
        mid->flush();

        osmium::memory::Buffer outbuf{4096,
                                      osmium::memory::Buffer::auto_grow::yes};

        auto const mid_q = mid->get_query_instance();
        REQUIRE_FALSE(mid_q->ways_get(way_id, outbuf));

        mid->commit();
    }

    // Set up middle again, still in "append" mode.
    {
        auto mid = std::make_shared<middle_pgsql_t>(&options);
        mid->start();

        idlist_t nds{11, 12};

        // create new version of the way
        mid->ways_set(buffer.add_way_and_get(way_id, nds));
        mid->flush();

        osmium::memory::Buffer outbuf{4096,
                                      osmium::memory::Buffer::auto_grow::yes};

        auto const mid_q = mid->get_query_instance();
        REQUIRE(mid_q->ways_get(way_id, outbuf));

        auto &way = outbuf.get<osmium::Way>(0);

        REQUIRE(way.id() == way_id);
        REQUIRE(way.nodes().size() == nds.size());
        REQUIRE(way.nodes()[0].ref() == 11);
        REQUIRE(way.nodes()[1].ref() == 12);

        REQUIRE(mid_q->nodes_get_list(&(way.nodes())) == nds.size());
        REQUIRE(way.nodes()[0].location() == osmium::Location{1.1, 0.0});
        REQUIRE(way.nodes()[1].location() == osmium::Location{1.2, 0.0});

        mid->commit();
    }
}
