#include <catch.hpp>

#include <osmium/osm/crc.hpp>
#include <osmium/osm/crc_zlib.hpp>

#include "dependency-manager.hpp"
#include "middle-pgsql.hpp"
#include "middle-ram.hpp"

#include "common-cleanup.hpp"
#include "common-options.hpp"
#include "common-pg.hpp"

#include <algorithm>

static pg::tempdb_t db;

namespace {

/**
 * Wrapper around an Osmium buffer to create test objects in with some
 * convenience.
 */
class test_buffer_t
{
public:
    size_t add_node(osmid_t id, double lon, double lat)
    {
        using namespace osmium::builder::attr;
        return osmium::builder::add_node(buf, _id(id), _location(lon, lat));
    }

    size_t
    add_way(osmid_t wid, idlist_t const &ids,
            std::initializer_list<std::pair<char const *, char const *>> tags)
    {
        using namespace osmium::builder::attr;
        return osmium::builder::add_way(buf, _id(wid), _nodes(ids),
                                        _tags(tags));
    }

    size_t add_relation(
        osmid_t rid,
        std::initializer_list<osmium::builder::attr::member_type> members,
        std::initializer_list<std::pair<char const *, char const *>> tags)
    {
        using namespace osmium::builder::attr;
        return osmium::builder::add_relation(
            buf, _id(rid), _members(members.begin(), members.end()),
            _tags(tags));
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

    osmium::Way &add_way_and_get(
        osmid_t wid, idlist_t const &ids,
        std::initializer_list<std::pair<char const *, char const *>> tags = {})
    {
        return get<osmium::Way>(add_way(wid, ids, tags));
    }

    osmium::Relation &add_relation_and_get(
        osmid_t rid,
        std::initializer_list<osmium::builder::attr::member_type> members,
        std::initializer_list<std::pair<char const *, char const *>> tags = {})
    {
        return get<osmium::Relation>(add_relation(rid, members, tags));
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

struct options_slim_with_schema
{
    static options_t options(pg::tempdb_t const &tmpdb)
    {
        options_t o = testing::opt_t().slim(tmpdb);
        o.middle_dbschema = "osm";
        return o;
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
                   options_slim_with_schema, options_slim_dense_cache,
                   options_ram_optimized, options_ram_flatnode)
{
    options_t const options = TestType::options(db);
    testing::cleanup::file_t flatnode_cleaner{
        options.flat_node_file.get_value_or("")};

    auto conn = db.connect();
    auto const num_tables =
        conn.get_count("pg_tables", "schemaname = 'public'");
    auto const num_indexes =
        conn.get_count("pg_indexes", "schemaname = 'public'");
    auto const num_procs =
        conn.get_count("pg_proc", "pronamespace = (SELECT oid FROM "
                                  "pg_namespace WHERE nspname = 'public')");

    if (!options.middle_dbschema.empty()) {
        conn.exec("CREATE SCHEMA IF NOT EXISTS osm;");
    }

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
        mid->node_set(node);
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
            mid->node_set(buffer.get<osmium::Node>(nodes.back()));
        }

        // set the way
        mid->way_set(buffer.add_way_and_get(way_id, nds));

        mid->flush();

        // get it back
        osmium::memory::Buffer outbuf{4096,
                                      osmium::memory::Buffer::auto_grow::yes};

        REQUIRE(mid_q->way_get(way_id, outbuf));

        auto &way = outbuf.get<osmium::Way>(0);

        CHECK(way.id() == way_id);
        REQUIRE(way.nodes().size() == nds.size());

        REQUIRE(mid_q->nodes_get_list(&(way.nodes())) == nds.size());
        for (osmid_t i = 1; i <= 10; ++i) {
            CHECK(way.nodes()[(size_t)i - 1].ref() == i);
        }

        // other ways are not retrievable
        REQUIRE_FALSE(mid_q->way_get(way_id + 1, outbuf));
    }

    SECTION("Set and retrieve a single relation with supporting ways")
    {
        idlist_t const nds[] = {{4, 5, 13, 14, 342}, {45, 90}, {30, 3, 45}};

        // set the node
        mid->node_set(buffer.add_node_and_get(1, 4.1, 12.8));

        // set the ways
        osmid_t wid = 10;
        for (auto const &n : nds) {
            mid->way_set(buffer.add_way_and_get(wid, n));
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

        mid->relation_set(relation);

        mid->flush();

        // retrieve the relation
        osmium::memory::Buffer outbuf{4096,
                                      osmium::memory::Buffer::auto_grow::yes};
        REQUIRE(mid_q->relation_get(123, outbuf));
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
        REQUIRE_FALSE(mid_q->relation_get(999, outbuf));
    }

    if (!options.middle_dbschema.empty()) {
        REQUIRE(num_tables ==
                conn.get_count("pg_tables", "schemaname = 'public'"));
        REQUIRE(num_indexes ==
                conn.get_count("pg_indexes", "schemaname = 'public'"));
        REQUIRE(num_procs ==
                conn.get_count("pg_proc",
                               "pronamespace = (SELECT oid FROM "
                               "pg_namespace WHERE nspname = 'public')"));
    }
}

/**
 * Check that the node is in the mid with the right id and location.
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

/// Return true if the node with the specified id is not in the mid.
static bool no_node(std::shared_ptr<middle_pgsql_t> const &mid, osmid_t id)
{
    test_buffer_t buffer;
    auto &nodes = buffer.add_nodes_and_get({id});
    auto const mid_q = mid->get_query_instance();
    return mid_q->nodes_get_list(&nodes) == 0;
}

TEMPLATE_TEST_CASE("middle: add, delete and update node", "",
                   options_slim_default, options_slim_dense_cache,
                   options_flat_node_cache)
{
    options_t options = TestType::options(db);

    testing::cleanup::file_t flatnode_cleaner{
        options.flat_node_file.get_value_or("")};

    // Prepare a buffer with some nodes which we will add and change.
    test_buffer_t buffer;
    auto const &node10 = buffer.add_node_and_get(10, 1.0, 0.0);
    auto const &node11 = buffer.add_node_and_get(11, 1.1, 0.0);
    auto const &node12 = buffer.add_node_and_get(12, 1.2, 0.0);

    auto const &node10a = buffer.add_node_and_get(10, 1.0, 1.0);

    // Set up middle in "create" mode to get a cleanly initialized database
    // and add some nodes. Does this in its own scope so that the mid is
    // closed properly.
    {
        auto mid = std::make_shared<middle_pgsql_t>(&options);

        mid->start();

        mid->node_set(node10);
        mid->node_set(node11);
        mid->flush();

        check_node(mid, node10);
        check_node(mid, node11);
        mid->commit();
    }

    // From now on use append mode to not destroy the data we just added.
    options.append = true;

    SECTION("Added nodes are there and no others")
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

            mid->node_delete(5);
            mid->node_delete(10);
            mid->node_delete(42);
            mid->flush();

            REQUIRE(no_node(mid, 5));
            REQUIRE(no_node(mid, 10));
            check_node(mid, node11);
            REQUIRE(no_node(mid, 42));

            mid->commit();
        }
        {
            // Check with a new mid.
            auto mid = std::make_shared<middle_pgsql_t>(&options);
            mid->start();

            REQUIRE(no_node(mid, 5));
            REQUIRE(no_node(mid, 10));
            check_node(mid, node11);
            REQUIRE(no_node(mid, 42));

            mid->commit();
        }
    }

    SECTION("Change (delete and set) existing and non-existing node")
    {
        {
            auto mid = std::make_shared<middle_pgsql_t>(&options);
            mid->start();

            mid->node_delete(10);
            mid->node_set(node10a);
            mid->node_delete(12);
            mid->node_set(node12);
            mid->flush();

            check_node(mid, node10a);
            check_node(mid, node11);
            check_node(mid, node12);

            mid->commit();
        }
        {
            // Check with a new mid.
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

            mid->node_set(node12);
            mid->flush();

            REQUIRE(no_node(mid, 5));
            check_node(mid, node10);
            check_node(mid, node11);
            check_node(mid, node12);
            REQUIRE(no_node(mid, 42));

            mid->commit();
        }
        {
            // Check with a new mid.
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

/**
 * Check that the way is in the mid with the right attributes and tags.
 * Does not check node locations.
 */
static void check_way(std::shared_ptr<middle_pgsql_t> const &mid,
                      osmium::Way const &orig_way)
{
    auto const mid_q = mid->get_query_instance();

    osmium::memory::Buffer outbuf{4096, osmium::memory::Buffer::auto_grow::yes};
    REQUIRE(mid_q->way_get(orig_way.id(), outbuf));
    auto const &way = outbuf.get<osmium::Way>(0);

    osmium::CRC<osmium::CRC_zlib> orig_crc;
    orig_crc.update(orig_way);

    osmium::CRC<osmium::CRC_zlib> test_crc;
    test_crc.update(way);

    REQUIRE(orig_crc().checksum() == test_crc().checksum());
}

/**
 * Check that the nodes (ids and locations) of the way with the way_id in the
 * mid are identical to the nodes in the nodes vector.
 */
static void check_way_nodes(std::shared_ptr<middle_pgsql_t> const &mid,
                            osmid_t way_id,
                            std::vector<osmium::Node const *> const &nodes)
{
    auto const mid_q = mid->get_query_instance();

    osmium::memory::Buffer outbuf{4096, osmium::memory::Buffer::auto_grow::yes};
    REQUIRE(mid_q->way_get(way_id, outbuf));
    auto &way = outbuf.get<osmium::Way>(0);

    REQUIRE(mid_q->nodes_get_list(&way.nodes()) == way.nodes().size());
    REQUIRE(way.nodes().size() == nodes.size());

    REQUIRE(std::equal(way.nodes().cbegin(), way.nodes().cend(), nodes.cbegin(),
                       [](osmium::NodeRef const &nr, osmium::Node const *node) {
                           return nr.ref() == node->id() &&
                                  nr.location() == node->location();
                       }));
}

/// Return true if the way with the specified id is not in the mid.
static bool no_way(std::shared_ptr<middle_pgsql_t> const &mid, osmid_t id)
{
    auto const mid_q = mid->get_query_instance();
    osmium::memory::Buffer outbuf{4096, osmium::memory::Buffer::auto_grow::yes};
    return !mid_q->way_get(id, outbuf);
}

TEMPLATE_TEST_CASE("middle: add, delete and update way", "",
                   options_slim_default, options_slim_dense_cache,
                   options_flat_node_cache)
{
    options_t options = TestType::options(db);

    testing::cleanup::file_t flatnode_cleaner{
        options.flat_node_file.get_value_or("")};

    // Create some ways we'll use for the tests.
    test_buffer_t buffer;
    auto const &way20 = buffer.add_way_and_get(
        20, {10, 11}, {{"highway", "residential"}, {"name", "High Street"}});

    auto const &way21 = buffer.add_way_and_get(21, {11, 12});

    auto const &way22 =
        buffer.add_way_and_get(22, {12, 10}, {{"power", "line"}});

    auto const &way20a = buffer.add_way_and_get(
        20, {10, 12}, {{"highway", "primary"}, {"name", "High Street"}});

    // Set up middle in "create" mode to get a cleanly initialized database and
    // add some ways. Does this in its own scope so that the mid is closed
    // properly.
    {
        auto mid = std::make_shared<middle_pgsql_t>(&options);
        mid->start();

        mid->way_set(way20);
        mid->way_set(way21);
        mid->flush();

        check_way(mid, way20);
        check_way(mid, way21);

        mid->commit();
    }

    // From now on use append mode to not destroy the data we just added.
    options.append = true;

    SECTION("Added ways are there and no others")
    {
        auto mid = std::make_shared<middle_pgsql_t>(&options);
        mid->start();

        REQUIRE(no_way(mid, 5));
        check_way(mid, way20);
        check_way(mid, way21);
        REQUIRE(no_way(mid, 22));

        mid->commit();
    }

    SECTION("Delete existing and non-existing way")
    {
        {
            auto mid = std::make_shared<middle_pgsql_t>(&options);
            mid->start();

            mid->way_delete(5);
            mid->way_delete(20);
            mid->way_delete(42);
            mid->flush();

            REQUIRE(no_way(mid, 5));
            REQUIRE(no_way(mid, 20));
            check_way(mid, way21);
            REQUIRE(no_way(mid, 42));

            mid->commit();
        }
        {
            // Check with a new mid.
            auto mid = std::make_shared<middle_pgsql_t>(&options);
            mid->start();

            REQUIRE(no_way(mid, 5));
            REQUIRE(no_way(mid, 20));
            check_way(mid, way21);
            REQUIRE(no_way(mid, 42));

            mid->commit();
        }
    }

    SECTION("Change (delete and set) existing and non-existing way")
    {
        {
            auto mid = std::make_shared<middle_pgsql_t>(&options);
            mid->start();

            mid->way_delete(20);
            mid->way_set(way20a);
            mid->way_delete(22);
            mid->way_set(way22);
            mid->flush();

            REQUIRE(no_way(mid, 5));
            check_way(mid, way20a);
            check_way(mid, way21);
            check_way(mid, way22);
            REQUIRE(no_way(mid, 42));

            mid->commit();
        }
        {
            // Check with a new mid.
            auto mid = std::make_shared<middle_pgsql_t>(&options);
            mid->start();

            REQUIRE(no_way(mid, 5));
            check_way(mid, way20a);
            check_way(mid, way21);
            check_way(mid, way22);
            REQUIRE(no_way(mid, 42));

            mid->commit();
        }
    }

    SECTION("Add new way")
    {
        {
            auto mid = std::make_shared<middle_pgsql_t>(&options);
            mid->start();

            mid->way_set(way22);
            mid->flush();

            REQUIRE(no_way(mid, 5));
            check_way(mid, way20);
            check_way(mid, way21);
            check_way(mid, way22);
            REQUIRE(no_way(mid, 42));

            mid->commit();
        }
        {
            // Check with a new mid.
            auto mid = std::make_shared<middle_pgsql_t>(&options);
            mid->start();

            REQUIRE(no_way(mid, 5));
            check_way(mid, way20);
            check_way(mid, way21);
            check_way(mid, way22);
            REQUIRE(no_way(mid, 42));

            mid->commit();
        }
    }
}

TEMPLATE_TEST_CASE("middle: add way with attributes", "", options_slim_default,
                   options_slim_dense_cache, options_flat_node_cache)
{
    options_t options = TestType::options(db);

    SECTION("With attributes") { options.extra_attributes = true; }
    SECTION("No attributes") { options.extra_attributes = false; }

    testing::cleanup::file_t flatnode_cleaner{
        options.flat_node_file.get_value_or("")};

    // Create some ways we'll use for the tests.
    test_buffer_t buffer;
    auto &way20 = buffer.add_way_and_get(
        20, {10, 11}, {{"highway", "residential"}, {"name", "High Street"}});
    way20.set_version(123);
    way20.set_timestamp(1234567890);
    way20.set_changeset(456);
    way20.set_uid(789);

    // The same way but with default attributes.
    auto &way20_no_attr = buffer.add_way_and_get(
        20, {10, 11}, {{"highway", "residential"}, {"name", "High Street"}});

    // The same way but with attributes in tags.
    // The order of the tags is important here!
    auto &way20_attr_tags =
        buffer.add_way_and_get(20, {10, 11},
                               {{"highway", "residential"},
                                {"name", "High Street"},
                                {"osm_user", ""},
                                {"osm_uid", "789"},
                                {"osm_version", "123"},
                                {"osm_timestamp", "2009-02-13T23:31:30Z"},
                                {"osm_changeset", "456"}});

    {
        auto mid = std::make_shared<middle_pgsql_t>(&options);
        mid->start();

        mid->way_set(way20);
        mid->flush();

        check_way(mid,
                  options.extra_attributes ? way20_attr_tags : way20_no_attr);

        mid->commit();
    }

    // From now on use append mode to not destroy the data we just added.
    options.append = true;

    {
        auto mid = std::make_shared<middle_pgsql_t>(&options);
        mid->start();

        check_way(mid,
                  options.extra_attributes ? way20_attr_tags : way20_no_attr);

        mid->commit();
    }
}

/**
 * Check that the relation is in the mid with the right attributes, members
 * and tags. Only checks the relation, does not recurse into members.
 */
static void check_relation(std::shared_ptr<middle_pgsql_t> const &mid,
                           osmium::Relation const &orig_relation)
{
    auto const mid_q = mid->get_query_instance();

    osmium::memory::Buffer outbuf{4096, osmium::memory::Buffer::auto_grow::yes};
    REQUIRE(mid_q->relation_get(orig_relation.id(), outbuf));
    auto const &relation = outbuf.get<osmium::Relation>(0);

    osmium::CRC<osmium::CRC_zlib> orig_crc;
    orig_crc.update(orig_relation);

    osmium::CRC<osmium::CRC_zlib> test_crc;
    test_crc.update(relation);

    REQUIRE(orig_crc().checksum() == test_crc().checksum());
}

/// Return true if the relation with the specified id is not in the mid.
static bool no_relation(std::shared_ptr<middle_pgsql_t> const &mid, osmid_t id)
{
    auto const mid_q = mid->get_query_instance();
    osmium::memory::Buffer outbuf{4096, osmium::memory::Buffer::auto_grow::yes};
    return !mid_q->relation_get(id, outbuf);
}

TEMPLATE_TEST_CASE("middle: add, delete and update relation", "",
                   options_slim_default, options_slim_dense_cache,
                   options_flat_node_cache)
{
    options_t options = TestType::options(db);

    testing::cleanup::file_t flatnode_cleaner{
        options.flat_node_file.get_value_or("")};

    // Create some relations we'll use for the tests.
    test_buffer_t buffer;
    using otype = osmium::item_type;
    auto const &relation30 = buffer.add_relation_and_get(
        30, {{otype::way, 10, "outer"}, {otype::way, 11, "inner"}},
        {{"type", "multipolygon"}, {"name", "Penguin Park"}});

    auto const &relation31 =
        buffer.add_relation_and_get(31, {{otype::node, 10, ""}});

    auto const &relation32 = buffer.add_relation_and_get(
        32, {{otype::relation, 39, ""}}, {{"type", "site"}});

    auto const &relation30a = buffer.add_relation_and_get(
        30, {{otype::way, 10, "outer"}, {otype::way, 11, "outer"}},
        {{"type", "multipolygon"}, {"name", "Pigeon Park"}});

    // Set up middle in "create" mode to get a cleanly initialized database and
    // add some relations. Does this in its own scope so that the mid is closed
    // properly.
    {
        auto mid = std::make_shared<middle_pgsql_t>(&options);
        mid->start();

        mid->relation_set(relation30);
        mid->relation_set(relation31);
        mid->flush();

        check_relation(mid, relation30);
        check_relation(mid, relation31);

        mid->commit();
    }

    // From now on use append mode to not destroy the data we just added.
    options.append = true;

    SECTION("Added relations are there and no others")
    {
        auto mid = std::make_shared<middle_pgsql_t>(&options);
        mid->start();

        REQUIRE(no_relation(mid, 5));
        check_relation(mid, relation30);
        check_relation(mid, relation31);
        REQUIRE(no_relation(mid, 32));

        mid->commit();
    }

    SECTION("Delete existing and non-existing relation")
    {
        {
            auto mid = std::make_shared<middle_pgsql_t>(&options);
            mid->start();

            mid->relation_delete(5);
            mid->relation_delete(30);
            mid->relation_delete(42);
            mid->flush();

            REQUIRE(no_relation(mid, 5));
            REQUIRE(no_relation(mid, 30));
            check_relation(mid, relation31);
            REQUIRE(no_relation(mid, 42));

            mid->commit();
        }
        {
            // Check with a new mid.
            auto mid = std::make_shared<middle_pgsql_t>(&options);
            mid->start();

            REQUIRE(no_relation(mid, 5));
            REQUIRE(no_relation(mid, 30));
            check_relation(mid, relation31);
            REQUIRE(no_relation(mid, 42));

            mid->commit();
        }
    }

    SECTION("Change (delete and set) existing and non-existing relation")
    {
        {
            auto mid = std::make_shared<middle_pgsql_t>(&options);
            mid->start();

            mid->relation_delete(30);
            mid->relation_set(relation30a);
            mid->relation_delete(32);
            mid->relation_set(relation32);
            mid->flush();

            REQUIRE(no_relation(mid, 5));
            check_relation(mid, relation30a);
            check_relation(mid, relation31);
            check_relation(mid, relation32);
            REQUIRE(no_relation(mid, 42));

            mid->commit();
        }
        {
            // Check with a new mid.
            auto mid = std::make_shared<middle_pgsql_t>(&options);
            mid->start();

            REQUIRE(no_relation(mid, 5));
            check_relation(mid, relation30a);
            check_relation(mid, relation31);
            check_relation(mid, relation32);
            REQUIRE(no_relation(mid, 42));

            mid->commit();
        }
    }

    SECTION("Add new relation")
    {
        {
            auto mid = std::make_shared<middle_pgsql_t>(&options);
            mid->start();

            mid->relation_set(relation32);
            mid->flush();

            REQUIRE(no_relation(mid, 5));
            check_relation(mid, relation30);
            check_relation(mid, relation31);
            check_relation(mid, relation32);
            REQUIRE(no_relation(mid, 42));

            mid->commit();
        }
        {
            // Check with a new mid.
            auto mid = std::make_shared<middle_pgsql_t>(&options);
            mid->start();

            REQUIRE(no_relation(mid, 5));
            check_relation(mid, relation30);
            check_relation(mid, relation31);
            check_relation(mid, relation32);
            REQUIRE(no_relation(mid, 42));

            mid->commit();
        }
    }
}

TEMPLATE_TEST_CASE("middle: add relation with attributes", "",
                   options_slim_default, options_slim_dense_cache,
                   options_flat_node_cache)
{
    options_t options = TestType::options(db);

    SECTION("With attributes") { options.extra_attributes = true; }
    SECTION("No attributes") { options.extra_attributes = false; }

    testing::cleanup::file_t flatnode_cleaner{
        options.flat_node_file.get_value_or("")};

    // Create some relations we'll use for the tests.
    test_buffer_t buffer;
    using otype = osmium::item_type;
    auto &relation30 = buffer.add_relation_and_get(
        30, {{otype::way, 10, "outer"}, {otype::way, 11, "inner"}},
        {{"type", "multipolygon"}, {"name", "Penguin Park"}});
    relation30.set_version(123);
    relation30.set_timestamp(1234567890);
    relation30.set_changeset(456);
    relation30.set_uid(789);

    // The same relation but with default attributes.
    auto &relation30_no_attr = buffer.add_relation_and_get(
        30, {{otype::way, 10, "outer"}, {otype::way, 11, "inner"}},
        {{"type", "multipolygon"}, {"name", "Penguin Park"}});

    // The same relation but with attributes in tags.
    // The order of the tags is important here!
    auto &relation30_attr_tags = buffer.add_relation_and_get(
        30, {{otype::way, 10, "outer"}, {otype::way, 11, "inner"}},
        {{"type", "multipolygon"},
         {"name", "Penguin Park"},
         {"osm_user", ""},
         {"osm_uid", "789"},
         {"osm_version", "123"},
         {"osm_timestamp", "2009-02-13T23:31:30Z"},
         {"osm_changeset", "456"}});

    {
        auto mid = std::make_shared<middle_pgsql_t>(&options);
        mid->start();

        mid->relation_set(relation30);
        mid->flush();

        check_relation(mid, options.extra_attributes ? relation30_attr_tags
                                                     : relation30_no_attr);

        mid->commit();
    }

    // From now on use append mode to not destroy the data we just added.
    options.append = true;

    {
        auto mid = std::make_shared<middle_pgsql_t>(&options);
        mid->start();

        check_relation(mid, options.extra_attributes ? relation30_attr_tags
                                                     : relation30_no_attr);

        mid->commit();
    }
}

TEMPLATE_TEST_CASE("middle: change nodes in way", "", options_slim_default,
                   options_slim_dense_cache, options_flat_node_cache)
{
    options_t options = TestType::options(db);

    testing::cleanup::file_t flatnode_cleaner{
        options.flat_node_file.get_value_or("")};

    // create some nodes and ways we'll use for the tests
    test_buffer_t buffer;
    auto const &node10 = buffer.add_node_and_get(10, 1.0, 0.0);
    auto const &node11 = buffer.add_node_and_get(11, 1.1, 0.0);
    auto const &node12 = buffer.add_node_and_get(12, 1.2, 0.0);
    auto const &node10a = buffer.add_node_and_get(10, 2.0, 0.0);

    auto const &way20 = buffer.add_way_and_get(20, {10, 11});
    auto const &way21 = buffer.add_way_and_get(21, {11, 12});
    auto const &way22 = buffer.add_way_and_get(22, {12, 10});
    auto const &way20a = buffer.add_way_and_get(20, {11, 12});

    // Set up middle in "create" mode to get a cleanly initialized database and
    // add some nodes and ways. Does this in its own scope so that the mid is
    // closed properly.
    {
        auto mid = std::make_shared<middle_pgsql_t>(&options);
        full_dependency_manager_t dependency_manager{mid};
        mid->start();

        mid->node_set(node10);
        mid->node_set(node11);
        mid->node_set(node12);
        mid->flush();
        mid->way_set(way20);
        mid->way_set(way21);
        mid->flush();

        check_node(mid, node10);
        check_node(mid, node11);
        check_node(mid, node12);
        check_way(mid, way20);
        check_way_nodes(mid, way20.id(), {&node10, &node11});
        check_way(mid, way21);
        check_way_nodes(mid, way21.id(), {&node11, &node12});

        REQUIRE_FALSE(dependency_manager.has_pending());

        mid->commit();
    }

    // From now on use append mode to not destroy the data we just added.
    options.append = true;

    SECTION("Single way affected")
    {
        auto mid = std::make_shared<middle_pgsql_t>(&options);
        full_dependency_manager_t dependency_manager{mid};
        mid->start();

        mid->node_delete(10);
        mid->node_set(node10a);
        dependency_manager.node_changed(10);
        mid->flush();

        REQUIRE(dependency_manager.has_pending());
        idlist_t const way_ids = dependency_manager.get_pending_way_ids();
        REQUIRE_THAT(way_ids, Catch::Equals<osmid_t>({20}));

        check_way(mid, way20);
        check_way_nodes(mid, way20.id(), {&node10a, &node11});

        mid->commit();
    }

    SECTION("Two ways affected")
    {
        {
            auto mid = std::make_shared<middle_pgsql_t>(&options);
            mid->start();

            mid->way_set(way22);
            mid->flush();
            check_way(mid, way22);

            mid->commit();
        }
        {
            auto mid = std::make_shared<middle_pgsql_t>(&options);
            full_dependency_manager_t dependency_manager{mid};
            mid->start();

            mid->node_delete(10);
            mid->node_set(node10a);
            dependency_manager.node_changed(10);
            mid->flush();

            REQUIRE(dependency_manager.has_pending());
            idlist_t const way_ids = dependency_manager.get_pending_way_ids();
            REQUIRE_THAT(way_ids, Catch::Equals<osmid_t>({20, 22}));

            check_way(mid, way20);
            check_way_nodes(mid, way20.id(), {&node10a, &node11});
            check_way(mid, way22);
            check_way_nodes(mid, way22.id(), {&node12, &node10a});

            mid->commit();
        }
    }

    SECTION("Change way so the changing node isn't in it any more")
    {
        {
            auto mid = std::make_shared<middle_pgsql_t>(&options);
            mid->start();

            mid->way_delete(20);
            mid->way_set(way20a);
            mid->flush();

            check_way(mid, way20a);
            check_way_nodes(mid, way20.id(), {&node11, &node12});

            mid->commit();
        }

        {
            auto mid = std::make_shared<middle_pgsql_t>(&options);
            full_dependency_manager_t dependency_manager{mid};
            mid->start();

            mid->node_delete(10);
            mid->node_set(node10a);
            dependency_manager.node_changed(10);
            mid->flush();

            REQUIRE_FALSE(dependency_manager.has_pending());

            mid->commit();
        }
    }
}

TEMPLATE_TEST_CASE("middle: change nodes in relation", "", options_slim_default,
                   options_slim_dense_cache, options_flat_node_cache)
{
    options_t options = TestType::options(db);

    testing::cleanup::file_t flatnode_cleaner{
        options.flat_node_file.get_value_or("")};

    // create some nodes, ways, and relations we'll use for the tests
    test_buffer_t buffer;
    auto const &node10 = buffer.add_node_and_get(10, 1.0, 0.0);
    auto const &node11 = buffer.add_node_and_get(11, 1.1, 0.0);
    auto const &node12 = buffer.add_node_and_get(12, 1.2, 0.0);
    auto const &node10a = buffer.add_node_and_get(10, 1.0, 1.0);
    auto const &node11a = buffer.add_node_and_get(11, 1.1, 1.0);

    auto const &way20 = buffer.add_way_and_get(20, {11, 12});

    using otype = osmium::item_type;
    auto const &rel30 = buffer.add_relation_and_get(30, {{otype::node, 10}});
    auto const &rel31 = buffer.add_relation_and_get(31, {{otype::way, 20}});

    // Set up middle in "create" mode to get a cleanly initialized database and
    // add some nodes and ways. Does this in its own scope so that the mid is
    // closed properly.
    {
        auto mid = std::make_shared<middle_pgsql_t>(&options);
        mid->start();

        mid->node_set(node10);
        mid->node_set(node11);
        mid->node_set(node12);
        mid->flush();
        mid->way_set(way20);
        mid->flush();
        mid->relation_set(rel30);
        mid->relation_set(rel31);
        mid->flush();

        mid->commit();
    }

    // From now on use append mode to not destroy the data we just added.
    options.append = true;

    SECTION("Single relation directly affected")
    {
        auto mid = std::make_shared<middle_pgsql_t>(&options);
        full_dependency_manager_t dependency_manager{mid};
        mid->start();

        mid->node_delete(10);
        mid->node_set(node10a);
        dependency_manager.node_changed(10);
        mid->flush();

        REQUIRE(dependency_manager.has_pending());
        idlist_t const rel_ids = dependency_manager.get_pending_relation_ids();

        REQUIRE_THAT(rel_ids, Catch::Equals<osmid_t>({30}));
        check_relation(mid, rel30);

        mid->commit();
    }

    SECTION("Single relation indirectly affected (through way)")
    {
        auto mid = std::make_shared<middle_pgsql_t>(&options);
        full_dependency_manager_t dependency_manager{mid};
        mid->start();

        mid->node_delete(11);
        mid->node_set(node11a);
        dependency_manager.node_changed(11);
        mid->flush();

        REQUIRE(dependency_manager.has_pending());
        idlist_t const way_ids = dependency_manager.get_pending_way_ids();
        REQUIRE_THAT(way_ids, Catch::Equals<osmid_t>({20}));
        idlist_t const rel_ids = dependency_manager.get_pending_relation_ids();
        REQUIRE_THAT(rel_ids, Catch::Equals<osmid_t>({31}));
        check_relation(mid, rel31);
    }
}
