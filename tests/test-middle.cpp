#include <catch.hpp>

#include <osmium/osm/crc.hpp>
#include <osmium/osm/crc_zlib.hpp>

#include "middle-pgsql.hpp"
#include "middle-ram.hpp"

#include "common-options.hpp"
#include "common-pg.hpp"

static pg::tempdb_t db;

namespace {

/// Simple osmium buffer to store object with some convenience.
struct test_buffer_t
{
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

struct options_slim_dense_cache : options_slim_default
{
    static options_t options(pg::tempdb_t const &tmpdb)
    {
        options_t o = options_slim_default::options(tmpdb);
        o.alloc_chunkwise = ALLOC_DENSE;
        return o;
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
        auto const pos = buffer.buf.committed();
        {
            using namespace osmium::builder::attr;
            using otype = osmium::item_type;
            osmium::builder::add_relation(
                buffer.buf, _id(123), _member(_member(otype::way, 11)),
                _member(_member(otype::way, 10, "outer")),
                _member(_member(otype::node, 1)),
                _member(_member(otype::way, 12, "inner")));
        }
        osmium::CRC<osmium::CRC_zlib> orig_crc;
        orig_crc.update(buffer.get<osmium::Relation>(pos));

        mid->relations_set(buffer.get<osmium::Relation>(pos));

        mid->flush();

        // retrieve the relation
        buffer.buf.clear();
        auto const &rel = buffer.get<osmium::Relation>(0);
        REQUIRE(mid_q->relations_get(123, buffer.buf));

        CHECK(rel.id() == 123);
        CHECK(rel.members().size() == 4);

        osmium::CRC<osmium::CRC_zlib> crc;
        crc.update(rel);
        CHECK(orig_crc().checksum() == crc().checksum());

        // retrive the supporting ways
        rolelist_t roles;
        REQUIRE(mid_q->rel_way_members_get(rel, &roles, buffer.buf) == 3);
        REQUIRE(roles.size() == 3);

        for (auto &w : buffer.buf.select<osmium::Way>()) {
            REQUIRE(w.id() >= 10);
            REQUIRE(w.id() <= 12);
            auto const &expected = nds[w.id() - 10];
            REQUIRE(w.nodes().size() == expected.size());
            for (size_t i = 0; i < expected.size(); ++i) {
                REQUIRE(w.nodes()[i].ref() == expected[i]);
            }
        }

        // other relations are not retrievable
        REQUIRE_FALSE(mid_q->relations_get(999, buffer.buf));
    }
}
