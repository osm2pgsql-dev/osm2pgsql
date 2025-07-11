/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2025 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <catch.hpp>

#include <algorithm>
#include <array>

#include <osmium/osm/crc.hpp>
#include <osmium/osm/crc_zlib.hpp>

#include "middle-pgsql.hpp"
#include "middle-ram.hpp"
#include "osmdata.hpp"
#include "output-null.hpp"
#include "output-requirements.hpp"

#include "common-buffer.hpp"
#include "common-cleanup.hpp"
#include "common-options.hpp"
#include "common-pg.hpp"

namespace {

testing::pg::tempdb_t db;

void check_locations_are_equal(osmium::Location a, osmium::Location b)
{
    CHECK(a.lat() == Approx(b.lat()));
    CHECK(a.lon() == Approx(b.lon()));
}

} // anonymous namespace

struct options_slim_default
{
    static options_t options(testing::pg::tempdb_t const &tmpdb)
    {
        return testing::opt_t().slim(tmpdb);
    }
};

struct options_slim_with_lc_prefix
{
    static options_t options(testing::pg::tempdb_t const &tmpdb)
    {
        options_t o = testing::opt_t().slim(tmpdb);
        o.prefix = "pre";
        return o;
    }
};

struct options_slim_with_uc_prefix
{
    static options_t options(testing::pg::tempdb_t const &tmpdb)
    {
        options_t o = testing::opt_t().slim(tmpdb);
        o.prefix = "PRE";
        return o;
    }
};

struct options_slim_with_schema
{
    static options_t options(testing::pg::tempdb_t const &tmpdb)
    {
        options_t o = testing::opt_t().slim(tmpdb);
        o.middle_dbschema = "osm";
        return o;
    }
};

struct options_flat_node_cache
{
    static options_t options(testing::pg::tempdb_t const &tmpdb)
    {
        return testing::opt_t().slim(tmpdb).flatnodes();
    }
};

struct options_ram_optimized
{
    static options_t options(testing::pg::tempdb_t const &)
    {
        return testing::opt_t();
    }
};

TEMPLATE_TEST_CASE("middle import", "", options_slim_default,
                   options_slim_with_lc_prefix, options_slim_with_uc_prefix,
                   options_slim_with_schema, options_ram_optimized)
{
    options_t const options = TestType::options(db);
    testing::cleanup::file_t const flatnode_cleaner{options.flat_node_file};

    auto conn = db.connect();
    auto const num_tables =
        conn.get_count("pg_catalog.pg_tables", "schemaname = 'public'");
    auto const num_indexes =
        conn.get_count("pg_catalog.pg_indexes", "schemaname = 'public'");
    auto const num_procs =
        conn.get_count("pg_catalog.pg_proc",
                       "pronamespace = (SELECT oid FROM "
                       "pg_catalog.pg_namespace WHERE nspname = 'public')");

    if (options.middle_dbschema == "osm") {
        conn.exec("CREATE SCHEMA IF NOT EXISTS osm;");
    }

    auto thread_pool = std::make_shared<thread_pool_t>(1U);
    auto mid = create_middle(thread_pool, options);
    mid->start();

    output_requirements requirements;
    requirements.full_ways = true;
    requirements.full_relations = true;
    mid->set_requirements(requirements);

    auto const mid_q = mid->get_query_instance();

    test_buffer_t buffer;

    SECTION("Set and retrieve a single node")
    {
        auto const &node = buffer.add_node("n1234 x98.7654321 y12.3456789");

        // set the node
        mid->node(node);
        mid->after_nodes();
        mid->after_ways();
        mid->after_relations();

        // getting it back works only via a waylist
        auto &nodes = buffer.add_way("w3 Nn1234").nodes();

        // get it back
        REQUIRE(mid_q->nodes_get_list(&nodes) == nodes.size());
        check_locations_are_equal(nodes[0].location(), node.location());

        // other nodes are not retrievable
        auto &n2 = buffer.add_way("w3 Nn1,n2,n1235").nodes();
        REQUIRE(mid_q->nodes_get_list(&n2) == 0);

        // check the same thing again using get_node_location() function
        check_locations_are_equal(mid_q->get_node_location(1234),
                                  node.location());
        CHECK_FALSE(mid_q->get_node_location(1).valid());
        CHECK_FALSE(mid_q->get_node_location(2).valid());
        CHECK_FALSE(mid_q->get_node_location(1235).valid());
    }

    SECTION("Set and retrieve a single way")
    {
        osmid_t const way_id = 1;
        double const lon = 98.7654321;
        double const lat = 12.3456789;
        idlist_t nds;

        // set nodes
        for (osmid_t i = 1; i <= 10; ++i) {
            nds.push_back(i);
            auto const &node = buffer.add_node(fmt::format(
                "n{} x{:.7f} y{:.7f}", i, lon - static_cast<double>(i) * 0.003,
                lat + static_cast<double>(i) * 0.001));
            mid->node(node);
        }
        mid->after_nodes();

        // set the way
        mid->way(buffer.add_way(way_id, nds));
        mid->after_ways();
        mid->after_relations();

        // get it back
        osmium::memory::Buffer outbuf{4096,
                                      osmium::memory::Buffer::auto_grow::yes};

        REQUIRE(mid_q->way_get(way_id, &outbuf));

        auto &way = outbuf.get<osmium::Way>(0);

        CHECK(way.id() == way_id);
        REQUIRE(way.nodes().size() == nds.size());

        REQUIRE(mid_q->nodes_get_list(&(way.nodes())) == nds.size());
        for (osmid_t i = 1; i <= 10; ++i) {
            auto const &nr = way.nodes()[static_cast<size_t>(i) - 1];
            CHECK(nr.ref() == i);
            CHECK(nr.location().lon() == Approx(lon - i * 0.003));
            CHECK(nr.location().lat() == Approx(lat + i * 0.001));
        }

        // other ways are not retrievable
        REQUIRE_FALSE(mid_q->way_get(way_id + 1, &outbuf));
    }

    SECTION("Set and retrieve a single relation with supporting ways")
    {
        std::array<idlist_t, 3> const nds = {
            {{4, 5, 13, 14, 342}, {45, 90}, {30, 3, 45}}};

        // set the node
        mid->node(buffer.add_node("n1 x4.1 y12.8"));
        mid->after_nodes();

        // set the ways
        osmid_t wid = 10;
        for (auto const &n : nds) {
            mid->way(buffer.add_way(wid, n));
            ++wid;
        }
        mid->after_ways();

        // set the relation
        auto const &relation =
            buffer.add_relation("r123 Mw11@,w10@outer,n1@,w12@inner");

        std::vector<std::pair<osmium::item_type, osmid_t>> const expected = {
            {osmium::item_type::way, 11},
            {osmium::item_type::way, 10},
            {osmium::item_type::node, 1},
            {osmium::item_type::way, 12},
        };

        osmium::CRC<osmium::CRC_zlib> orig_crc;
        orig_crc.update(relation);

        mid->relation(relation);
        mid->after_relations();

        // retrieve the relation
        osmium::memory::Buffer outbuf{4096,
                                      osmium::memory::Buffer::auto_grow::yes};
        REQUIRE(mid_q->relation_get(123, &outbuf));
        auto const &rel = outbuf.get<osmium::Relation>(0);

        CHECK(rel.id() == 123);
        CHECK(rel.members().size() == 4);

        osmium::CRC<osmium::CRC_zlib> crc;
        crc.update(rel);
        CHECK(orig_crc().checksum() == crc().checksum());

        // retrieve node members only
        osmium::memory::Buffer memberbuf{
            4096, osmium::memory::Buffer::auto_grow::yes};
        REQUIRE(mid_q->rel_members_get(rel, &memberbuf,
                                       osmium::osm_entity_bits::node) == 1);

        {
            auto const objects = memberbuf.select<osmium::OSMObject>();
            auto it = objects.cbegin();
            auto const end = objects.cend();
            for (auto const &p : expected) {
                if (p.first == osmium::item_type::node) {
                    REQUIRE(it != end);
                    REQUIRE(it->type() == p.first);
                    REQUIRE(it->id() == p.second);
                    ++it;
                }
            }
        }

        memberbuf.clear();

        // retrieve way members only
        REQUIRE(mid_q->rel_members_get(rel, &memberbuf,
                                       osmium::osm_entity_bits::way) == 3);

        {
            auto const objects = memberbuf.select<osmium::OSMObject>();
            auto it = objects.cbegin();
            auto const end = objects.cend();
            for (auto const &p : expected) {
                if (p.first == osmium::item_type::way) {
                    REQUIRE(it != end);
                    REQUIRE(it->type() == p.first);
                    REQUIRE(it->id() == p.second);
                    ++it;
                }
            }
        }

        memberbuf.clear();

        // retrieve all members
        REQUIRE(mid_q->rel_members_get(rel, &memberbuf,
                                       osmium::osm_entity_bits::node |
                                           osmium::osm_entity_bits::way) == 4);

        {
            auto const objects = memberbuf.select<osmium::OSMObject>();
            auto it = objects.cbegin();
            auto const end = objects.cend();
            for (auto const &p : expected) {
                REQUIRE(it != end);
                REQUIRE(it->type() == p.first);
                REQUIRE(it->id() == p.second);
                ++it;
            }
        }

        // other relations are not retrievable
        REQUIRE_FALSE(mid_q->relation_get(999, &outbuf));
    }

    if (options.middle_dbschema != "public") {
        REQUIRE(num_tables == conn.get_count("pg_catalog.pg_tables",
                                             "schemaname = 'public'"));
        REQUIRE(num_indexes == conn.get_count("pg_catalog.pg_indexes",
                                              "schemaname = 'public'"));
        REQUIRE(num_procs ==
                conn.get_count(
                    "pg_catalog.pg_proc",
                    "pronamespace = (SELECT oid FROM "
                    "pg_catalog.pg_namespace WHERE nspname = 'public')"));
    }
}

namespace {

/**
 * Check that the node is in the mid with the right id and location.
 */
void check_node(std::shared_ptr<middle_pgsql_t> const &mid,
                osmium::Node const &node)
{
    test_buffer_t buffer;
    auto &nodes = buffer.add_way(999, {node.id()}).nodes();
    auto const mid_q = mid->get_query_instance();
    REQUIRE(mid_q->nodes_get_list(&nodes) == 1);
    REQUIRE(nodes[0].ref() == node.id());
    REQUIRE(nodes[0].location() == node.location());
}

/// Return true if the node with the specified id is not in the mid.
bool no_node(std::shared_ptr<middle_pgsql_t> const &mid, osmid_t id)
{
    test_buffer_t buffer;
    auto &nodes = buffer.add_way(999, {id}).nodes();
    auto const mid_q = mid->get_query_instance();
    return mid_q->nodes_get_list(&nodes) == 0;
}

} // anonymous namespace

TEMPLATE_TEST_CASE("middle: add, delete and update node", "",
                   options_slim_default, options_flat_node_cache)
{
    auto thread_pool = std::make_shared<thread_pool_t>(1U);

    options_t options = TestType::options(db);

    testing::cleanup::file_t const flatnode_cleaner{options.flat_node_file};

    // Prepare a buffer with some nodes which we will add and change.
    test_buffer_t buffer;
    auto const &node10 = buffer.add_node("n10 x1.0 y0.0");
    auto const &node11 = buffer.add_node("n11 x1.1 y0.0");
    auto const &node12 = buffer.add_node("n12 x1.2 y0.0");

    auto const &node10a = buffer.add_node("n10 x1.0 y1.0");

    auto const &node5d = buffer.add_node("n5 dD");
    auto const &node10d = buffer.add_node("n10 dD");
    auto const &node12d = buffer.add_node("n12 dD");
    auto const &node42d = buffer.add_node("n42 dD");

    // Set up middle in "create" mode to get a cleanly initialized database
    // and add some nodes. Does this in its own scope so that the mid is
    // closed properly.
    {
        auto mid = std::make_shared<middle_pgsql_t>(thread_pool, &options);

        mid->start();

        mid->node(node10);
        mid->node(node11);
        mid->after_nodes();
        mid->after_ways();
        mid->after_relations();

        check_node(mid, node10);
        check_node(mid, node11);
    }

    // From now on use append mode to not destroy the data we just added.
    options.append = true;

    SECTION("Added nodes are there and no others")
    {
        auto mid = std::make_shared<middle_pgsql_t>(thread_pool, &options);
        mid->start();

        check_node(mid, node10);
        check_node(mid, node11);
        REQUIRE(no_node(mid, 5));
        REQUIRE(no_node(mid, 42));
    }

    SECTION("Delete existing and non-existing node")
    {
        {
            auto mid = std::make_shared<middle_pgsql_t>(thread_pool, &options);
            mid->start();

            mid->node(node5d);
            mid->node(node10d);
            mid->node(node42d);
            mid->after_nodes();
            mid->after_ways();
            mid->after_relations();

            REQUIRE(no_node(mid, 5));
            REQUIRE(no_node(mid, 10));
            check_node(mid, node11);
            REQUIRE(no_node(mid, 42));
        }
        {
            // Check with a new mid.
            auto mid = std::make_shared<middle_pgsql_t>(thread_pool, &options);
            mid->start();

            REQUIRE(no_node(mid, 5));
            REQUIRE(no_node(mid, 10));
            check_node(mid, node11);
            REQUIRE(no_node(mid, 42));
        }
    }

    SECTION("Change (delete and set) existing and non-existing node")
    {
        {
            auto mid = std::make_shared<middle_pgsql_t>(thread_pool, &options);
            mid->start();

            mid->node(node10d);
            mid->node(node10a);
            mid->node(node12d);
            mid->node(node12);
            mid->after_nodes();
            mid->after_ways();
            mid->after_relations();

            check_node(mid, node10a);
            check_node(mid, node11);
            check_node(mid, node12);
        }
        {
            // Check with a new mid.
            auto mid = std::make_shared<middle_pgsql_t>(thread_pool, &options);
            mid->start();

            check_node(mid, node10a);
            check_node(mid, node11);
            check_node(mid, node12);
        }
    }

    SECTION("Add new node")
    {
        {
            auto mid = std::make_shared<middle_pgsql_t>(thread_pool, &options);
            mid->start();

            mid->node(node12);
            mid->after_nodes();
            mid->after_ways();
            mid->after_relations();

            REQUIRE(no_node(mid, 5));
            check_node(mid, node10);
            check_node(mid, node11);
            check_node(mid, node12);
            REQUIRE(no_node(mid, 42));
        }
        {
            // Check with a new mid.
            auto mid = std::make_shared<middle_pgsql_t>(thread_pool, &options);
            mid->start();

            REQUIRE(no_node(mid, 5));
            check_node(mid, node10);
            check_node(mid, node11);
            check_node(mid, node12);
            REQUIRE(no_node(mid, 42));
        }
    }
}

namespace {

/**
 * Check that the way is in the mid with the right attributes and tags.
 * Does not check node locations.
 */
void check_way(std::shared_ptr<middle_pgsql_t> const &mid,
               osmium::Way const &orig_way)
{
    auto const mid_q = mid->get_query_instance();

    osmium::memory::Buffer outbuf{4096, osmium::memory::Buffer::auto_grow::yes};
    REQUIRE(mid_q->way_get(orig_way.id(), &outbuf));
    auto const &way = outbuf.get<osmium::Way>(0);

    CHECK(orig_way.id() == way.id());
    CHECK(orig_way.timestamp() == way.timestamp());
    CHECK(orig_way.version() == way.version());
    CHECK(orig_way.changeset() == way.changeset());
    CHECK(orig_way.uid() == way.uid());
    CHECK(std::strcmp(orig_way.user(), way.user()) == 0);

    REQUIRE(orig_way.tags().size() == way.tags().size());
    for (auto it1 = orig_way.tags().cbegin(), it2 = way.tags().cbegin();
         it1 != orig_way.tags().cend(); ++it1, ++it2) {
        CHECK(*it1 == *it2);
    }

    REQUIRE(orig_way.nodes().size() == way.nodes().size());
    for (auto it1 = orig_way.nodes().cbegin(), it2 = way.nodes().cbegin();
         it1 != orig_way.nodes().cend(); ++it1, ++it2) {
        CHECK(*it1 == *it2);
    }

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
void check_way_nodes(std::shared_ptr<middle_pgsql_t> const &mid, osmid_t way_id,
                     std::vector<osmium::Node const *> const &nodes)
{
    auto const mid_q = mid->get_query_instance();

    osmium::memory::Buffer outbuf{4096, osmium::memory::Buffer::auto_grow::yes};
    REQUIRE(mid_q->way_get(way_id, &outbuf));
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
bool no_way(std::shared_ptr<middle_pgsql_t> const &mid, osmid_t id)
{
    auto const mid_q = mid->get_query_instance();
    osmium::memory::Buffer outbuf{4096, osmium::memory::Buffer::auto_grow::yes};
    return !mid_q->way_get(id, &outbuf);
}

} // anonymous namespace

TEMPLATE_TEST_CASE("middle: add, delete and update way", "",
                   options_slim_default, options_flat_node_cache)
{
    auto thread_pool = std::make_shared<thread_pool_t>(1U);

    options_t options = TestType::options(db);

    testing::cleanup::file_t const flatnode_cleaner{options.flat_node_file};

    // Create some ways we'll use for the tests.
    test_buffer_t buffer;
    auto const &way20 =
        buffer.add_way("w20 Nn10,n11 Thighway=residential,name=High_Street");

    auto const &way21 = buffer.add_way("w21 Nn11,n12");

    auto const &way22 = buffer.add_way("w22 Nn12,n10 Tpower=line");

    auto &way20a =
        buffer.add_way("w20 Nn10,n12 Thighway=primary,name=High_Street");

    auto const &way5d = buffer.add_way("w5 dD");
    auto &way20d = buffer.add_way("w20 dD");
    auto &way22d = buffer.add_way("w22 dD");
    auto &way42d = buffer.add_way("w42 dD");

    // Set up middle in "create" mode to get a cleanly initialized database and
    // add some ways. Does this in its own scope so that the mid is closed
    // properly.
    {
        auto mid = std::make_shared<middle_pgsql_t>(thread_pool, &options);
        mid->start();

        mid->after_nodes();
        mid->way(way20);
        mid->way(way21);
        mid->after_ways();
        mid->after_relations();

        check_way(mid, way20);
        check_way(mid, way21);
    }

    // From now on use append mode to not destroy the data we just added.
    options.append = true;

    SECTION("Added ways are there and no others")
    {
        auto mid = std::make_shared<middle_pgsql_t>(thread_pool, &options);
        mid->start();

        REQUIRE(no_way(mid, 5));
        check_way(mid, way20);
        check_way(mid, way21);
        REQUIRE(no_way(mid, 22));
    }

    SECTION("Delete existing and non-existing way")
    {
        {
            auto mid = std::make_shared<middle_pgsql_t>(thread_pool, &options);
            mid->start();

            mid->after_nodes();
            mid->way(way5d);
            mid->way(way20d);
            mid->way(way42d);
            mid->after_ways();
            mid->after_relations();

            REQUIRE(no_way(mid, 5));
            REQUIRE(no_way(mid, 20));
            check_way(mid, way21);
            REQUIRE(no_way(mid, 42));
        }
        {
            // Check with a new mid.
            auto mid = std::make_shared<middle_pgsql_t>(thread_pool, &options);
            mid->start();

            REQUIRE(no_way(mid, 5));
            REQUIRE(no_way(mid, 20));
            check_way(mid, way21);
            REQUIRE(no_way(mid, 42));
        }
    }

    SECTION("Change (delete and set) existing and non-existing way")
    {
        {
            auto mid = std::make_shared<middle_pgsql_t>(thread_pool, &options);
            mid->start();

            mid->after_nodes();
            mid->way(way20d);
            mid->way(way20a);
            mid->way(way22d);
            mid->way(way22);
            mid->after_ways();
            mid->after_relations();

            REQUIRE(no_way(mid, 5));
            check_way(mid, way20a);
            check_way(mid, way21);
            check_way(mid, way22);
            REQUIRE(no_way(mid, 42));
        }
        {
            // Check with a new mid.
            auto mid = std::make_shared<middle_pgsql_t>(thread_pool, &options);
            mid->start();

            REQUIRE(no_way(mid, 5));
            check_way(mid, way20a);
            check_way(mid, way21);
            check_way(mid, way22);
            REQUIRE(no_way(mid, 42));
        }
    }

    SECTION("Add new way")
    {
        {
            auto mid = std::make_shared<middle_pgsql_t>(thread_pool, &options);
            mid->start();

            mid->after_nodes();
            mid->way(way22);
            mid->after_ways();
            mid->after_relations();

            REQUIRE(no_way(mid, 5));
            check_way(mid, way20);
            check_way(mid, way21);
            check_way(mid, way22);
            REQUIRE(no_way(mid, 42));
        }
        {
            // Check with a new mid.
            auto mid = std::make_shared<middle_pgsql_t>(thread_pool, &options);
            mid->start();

            REQUIRE(no_way(mid, 5));
            check_way(mid, way20);
            check_way(mid, way21);
            check_way(mid, way22);
            REQUIRE(no_way(mid, 42));
        }
    }
}

TEMPLATE_TEST_CASE("middle: add way with attributes", "", options_slim_default,
                   options_flat_node_cache)
{
    auto thread_pool = std::make_shared<thread_pool_t>(1U);

    options_t options = TestType::options(db);

    SECTION("With attributes") { options.extra_attributes = true; }
    SECTION("No attributes") { options.extra_attributes = false; }

    testing::cleanup::file_t const flatnode_cleaner{options.flat_node_file};

    // Create some ways we'll use for the tests.
    test_buffer_t buffer;
    auto &way20 =
        buffer.add_way("w20 Nn10,n11 Thighway=residential,name=High_Street");
    way20.set_version(123);
    way20.set_timestamp(1234567890);
    way20.set_changeset(456);
    way20.set_uid(789);

    // The same way but with default attributes.
    auto &way20_no_attr =
        buffer.add_way("w20 Nn10,n11 Thighway=residential,name=High_Street");

    {
        auto mid = std::make_shared<middle_pgsql_t>(thread_pool, &options);
        mid->start();

        mid->after_nodes();
        mid->way(way20);
        mid->after_ways();
        mid->after_relations();

        check_way(mid, options.extra_attributes ? way20 : way20_no_attr);
    }

    // From now on use append mode to not destroy the data we just added.
    options.append = true;

    {
        auto mid = std::make_shared<middle_pgsql_t>(thread_pool, &options);
        mid->start();

        check_way(mid, options.extra_attributes ? way20 : way20_no_attr);
    }
}

namespace {

/**
 * Check that the relation is in the mid with the right attributes, members
 * and tags. Only checks the relation, does not recurse into members.
 */
void check_relation(std::shared_ptr<middle_pgsql_t> const &mid,
                    osmium::Relation const &orig_relation)
{
    auto const mid_q = mid->get_query_instance();

    osmium::memory::Buffer outbuf{4096, osmium::memory::Buffer::auto_grow::yes};
    REQUIRE(mid_q->relation_get(orig_relation.id(), &outbuf));
    auto const &relation = outbuf.get<osmium::Relation>(0);

    CHECK(orig_relation.id() == relation.id());
    CHECK(orig_relation.timestamp() == relation.timestamp());
    CHECK(orig_relation.version() == relation.version());
    CHECK(orig_relation.changeset() == relation.changeset());
    CHECK(orig_relation.uid() == relation.uid());
    CHECK(std::strcmp(orig_relation.user(), relation.user()) == 0);

    REQUIRE(orig_relation.tags().size() == relation.tags().size());
    for (auto it1 = orig_relation.tags().cbegin(),
              it2 = relation.tags().cbegin();
         it1 != orig_relation.tags().cend(); ++it1, ++it2) {
        CHECK(*it1 == *it2);
    }

    REQUIRE(orig_relation.members().size() == relation.members().size());
    for (auto it1 = orig_relation.members().cbegin(),
              it2 = relation.members().cbegin();
         it1 != orig_relation.members().cend(); ++it1, ++it2) {
        CHECK(it1->type() == it2->type());
        CHECK(it1->ref() == it2->ref());
        CHECK(std::strcmp(it1->role(), it2->role()) == 0);
    }

    osmium::CRC<osmium::CRC_zlib> orig_crc;
    orig_crc.update(orig_relation);

    osmium::CRC<osmium::CRC_zlib> test_crc;
    test_crc.update(relation);

    REQUIRE(orig_crc().checksum() == test_crc().checksum());
}

/// Return true if the relation with the specified id is not in the mid.
bool no_relation(std::shared_ptr<middle_pgsql_t> const &mid, osmid_t id)
{
    auto const mid_q = mid->get_query_instance();
    osmium::memory::Buffer outbuf{4096, osmium::memory::Buffer::auto_grow::yes};
    return !mid_q->relation_get(id, &outbuf);
}

} // anonymous namespace

TEMPLATE_TEST_CASE("middle: add, delete and update relation", "",
                   options_slim_default, options_flat_node_cache)
{
    auto thread_pool = std::make_shared<thread_pool_t>(1U);

    options_t options = TestType::options(db);

    testing::cleanup::file_t const flatnode_cleaner{options.flat_node_file};

    // Create some relations we'll use for the tests.
    test_buffer_t buffer;
    auto const &relation30 = buffer.add_relation(
        "r30 Mw10@outer,w11@innder Tname=Penguin_Park,type=multipolygon");

    auto const &relation31 = buffer.add_relation("r31 Mn10@");

    auto const &relation32 = buffer.add_relation("r32 Mr39@ Ttype=site");

    auto const &relation30a = buffer.add_relation(
        "r30 Mw10@outer,w11@outer Tname=Pigeon_Park,type=multipolygon");

    auto const &relation5d = buffer.add_relation("r5 dD");
    auto const &relation30d = buffer.add_relation("r30 dD");
    auto const &relation32d = buffer.add_relation("r32 dD");
    auto const &relation42d = buffer.add_relation("r42 dD");

    // Set up middle in "create" mode to get a cleanly initialized database and
    // add some relations. Does this in its own scope so that the mid is closed
    // properly.
    {
        auto mid = std::make_shared<middle_pgsql_t>(thread_pool, &options);
        mid->start();

        mid->after_nodes();
        mid->after_ways();
        mid->relation(relation30);
        mid->relation(relation31);
        mid->after_relations();

        check_relation(mid, relation30);
        check_relation(mid, relation31);
    }

    // From now on use append mode to not destroy the data we just added.
    options.append = true;

    SECTION("Added relations are there and no others")
    {
        auto mid = std::make_shared<middle_pgsql_t>(thread_pool, &options);
        mid->start();

        REQUIRE(no_relation(mid, 5));
        check_relation(mid, relation30);
        check_relation(mid, relation31);
        REQUIRE(no_relation(mid, 32));
    }

    SECTION("Delete existing and non-existing relation")
    {
        {
            auto mid = std::make_shared<middle_pgsql_t>(thread_pool, &options);
            mid->start();

            mid->after_nodes();
            mid->after_ways();
            mid->relation(relation5d);
            mid->relation(relation30d);
            mid->relation(relation42d);
            mid->after_relations();

            REQUIRE(no_relation(mid, 5));
            REQUIRE(no_relation(mid, 30));
            check_relation(mid, relation31);
            REQUIRE(no_relation(mid, 42));
        }
        {
            // Check with a new mid.
            auto mid = std::make_shared<middle_pgsql_t>(thread_pool, &options);
            mid->start();

            REQUIRE(no_relation(mid, 5));
            REQUIRE(no_relation(mid, 30));
            check_relation(mid, relation31);
            REQUIRE(no_relation(mid, 42));
        }
    }

    SECTION("Change (delete and set) existing and non-existing relation")
    {
        {
            auto mid = std::make_shared<middle_pgsql_t>(thread_pool, &options);
            mid->start();

            mid->after_nodes();
            mid->after_ways();
            mid->relation(relation30d);
            mid->relation(relation30a);
            mid->relation(relation32d);
            mid->relation(relation32);
            mid->after_relations();

            REQUIRE(no_relation(mid, 5));
            check_relation(mid, relation30a);
            check_relation(mid, relation31);
            check_relation(mid, relation32);
            REQUIRE(no_relation(mid, 42));
        }
        {
            // Check with a new mid.
            auto mid = std::make_shared<middle_pgsql_t>(thread_pool, &options);
            mid->start();

            REQUIRE(no_relation(mid, 5));
            check_relation(mid, relation30a);
            check_relation(mid, relation31);
            check_relation(mid, relation32);
            REQUIRE(no_relation(mid, 42));
        }
    }

    SECTION("Add new relation")
    {
        {
            auto mid = std::make_shared<middle_pgsql_t>(thread_pool, &options);
            mid->start();

            mid->after_nodes();
            mid->after_ways();
            mid->relation(relation32);
            mid->after_relations();

            REQUIRE(no_relation(mid, 5));
            check_relation(mid, relation30);
            check_relation(mid, relation31);
            check_relation(mid, relation32);
            REQUIRE(no_relation(mid, 42));
        }
        {
            // Check with a new mid.
            auto mid = std::make_shared<middle_pgsql_t>(thread_pool, &options);
            mid->start();

            REQUIRE(no_relation(mid, 5));
            check_relation(mid, relation30);
            check_relation(mid, relation31);
            check_relation(mid, relation32);
            REQUIRE(no_relation(mid, 42));
        }
    }
}

TEMPLATE_TEST_CASE("middle: add relation with attributes", "",
                   options_slim_default, options_flat_node_cache)
{
    auto thread_pool = std::make_shared<thread_pool_t>(1U);

    options_t options = TestType::options(db);

    SECTION("With attributes") { options.extra_attributes = true; }
    SECTION("No attributes") { options.extra_attributes = false; }

    testing::cleanup::file_t const flatnode_cleaner{options.flat_node_file};

    // Create some relations we'll use for the tests.
    test_buffer_t buffer;
    auto const &relation30 = buffer.add_relation(
        "r30 v123 c456 i789 t2009-02-13T23:31:30Z Mw10@outer,w11@inner "
        "Tname=Penguin_Park,type=multipolygon");

    // The same relation but with default attributes.
    auto const &relation30_no_attr = buffer.add_relation(
        "r30 Mw10@outer,w11@inner Tname=Penguin_Park,type=multipolygon");

    {
        auto mid = std::make_shared<middle_pgsql_t>(thread_pool, &options);
        mid->start();

        mid->after_nodes();
        mid->after_ways();
        mid->relation(relation30);
        mid->after_relations();

        check_relation(mid, options.extra_attributes ? relation30
                                                     : relation30_no_attr);
    }

    // From now on use append mode to not destroy the data we just added.
    options.append = true;

    {
        auto mid = std::make_shared<middle_pgsql_t>(thread_pool, &options);
        mid->start();

        check_relation(mid, options.extra_attributes ? relation30
                                                     : relation30_no_attr);
    }
}

TEMPLATE_TEST_CASE("middle: change nodes in way", "", options_slim_default,
                   options_flat_node_cache)
{
    auto thread_pool = std::make_shared<thread_pool_t>(1U);

    options_t options = TestType::options(db);

    testing::cleanup::file_t const flatnode_cleaner{options.flat_node_file};

    // create some nodes and ways we'll use for the tests
    test_buffer_t buffer;
    auto const &node10 = buffer.add_node("n10 x1.0 y0.0");
    auto const &node11 = buffer.add_node("n11 x1.1 y0.0");
    auto const &node12 = buffer.add_node("n12 x1.2 y0.0");
    auto const &node10a = buffer.add_node("n10 x2.0 y0.0");

    auto const &node10d = buffer.add_node("n10 dD");

    auto &way20 = buffer.add_way("w20 Nn10,n11");
    auto &way21 = buffer.add_way("w21 Nn11,n12");
    auto &way22 = buffer.add_way("w22 Nn12,n10");
    auto &way20a = buffer.add_way("w20 Nn11,n12");

    auto &way20d = buffer.add_way("w20 dD");

    // Set up middle in "create" mode to get a cleanly initialized database and
    // add some nodes and ways. Does this in its own scope so that the mid is
    // closed properly.
    {
        auto mid = std::make_shared<middle_pgsql_t>(thread_pool, &options);
        mid->start();

        auto output = std::make_shared<output_null_t>(mid->get_query_instance(),
                                                      thread_pool, options);
        osmdata_t osmdata{mid, output, options};

        osmdata.node(node10);
        osmdata.node(node11);
        osmdata.node(node12);
        osmdata.after_nodes();
        osmdata.way(way20);
        osmdata.way(way21);
        osmdata.after_ways();
        osmdata.after_relations();

        check_node(mid, node10);
        check_node(mid, node11);
        check_node(mid, node12);
        check_way(mid, way20);
        check_way_nodes(mid, way20.id(), {&node10, &node11});
        check_way(mid, way21);
        check_way_nodes(mid, way21.id(), {&node11, &node12});

        REQUIRE(osmdata.get_pending_way_ids().empty());
        REQUIRE(osmdata.get_pending_relation_ids().empty());

        osmdata.stop();
    }

    // From now on use append mode to not destroy the data we just added.
    options.append = true;

    SECTION("Single way affected")
    {
        auto mid = std::make_shared<middle_pgsql_t>(thread_pool, &options);
        mid->start();

        auto output = std::make_shared<output_null_t>(mid->get_query_instance(),
                                                      thread_pool, options);
        osmdata_t osmdata{mid, output, options};

        osmdata.node(node10d);
        osmdata.node(node10a);
        osmdata.after_nodes();
        osmdata.after_ways();
        osmdata.after_relations();

        idlist_t const &way_ids = osmdata.get_pending_way_ids();
        REQUIRE(way_ids == idlist_t{20});

        REQUIRE(osmdata.get_pending_relation_ids().empty());

        check_way(mid, way20);
        check_way_nodes(mid, way20.id(), {&node10a, &node11});
    }

    SECTION("Two ways affected")
    {
        {
            auto mid = std::make_shared<middle_pgsql_t>(thread_pool, &options);
            mid->start();

            mid->after_nodes();
            mid->way(way22);
            mid->after_ways();
            mid->after_relations();

            check_way(mid, way22);
        }
        {
            auto mid = std::make_shared<middle_pgsql_t>(thread_pool, &options);
            mid->start();

            auto output = std::make_shared<output_null_t>(
                mid->get_query_instance(), thread_pool, options);
            osmdata_t osmdata{mid, output, options};

            osmdata.node(node10d);
            osmdata.node(node10a);
            osmdata.after_nodes();
            osmdata.after_ways();
            osmdata.after_relations();

            idlist_t const &way_ids = osmdata.get_pending_way_ids();
            REQUIRE(way_ids == idlist_t{20, 22});

            REQUIRE(osmdata.get_pending_relation_ids().empty());

            check_way(mid, way20);
            check_way_nodes(mid, way20.id(), {&node10a, &node11});
            check_way(mid, way22);
            check_way_nodes(mid, way22.id(), {&node12, &node10a});
        }
    }

    SECTION("Change way so the changing node isn't in it any more")
    {
        {
            auto mid = std::make_shared<middle_pgsql_t>(thread_pool, &options);
            mid->start();

            auto output = std::make_shared<output_null_t>(
                mid->get_query_instance(), thread_pool, options);
            osmdata_t osmdata{mid, output, options};

            osmdata.after_nodes();
            osmdata.way(way20d);
            osmdata.way(way20a);
            osmdata.after_ways();
            osmdata.after_relations();

            check_way(mid, way20a);
            check_way_nodes(mid, way20.id(), {&node11, &node12});
        }

        {
            auto mid = std::make_shared<middle_pgsql_t>(thread_pool, &options);
            mid->start();

            auto output = std::make_shared<output_null_t>(
                mid->get_query_instance(), thread_pool, options);
            osmdata_t osmdata{mid, output, options};

            osmdata.node(node10d);
            osmdata.node(node10a);
            osmdata.after_nodes();
            osmdata.after_ways();
            osmdata.after_relations();

            REQUIRE(osmdata.get_pending_way_ids().empty());
            REQUIRE(osmdata.get_pending_relation_ids().empty());
        }
    }
}

TEMPLATE_TEST_CASE("middle: change nodes in relation", "", options_slim_default,
                   options_flat_node_cache)
{
    auto thread_pool = std::make_shared<thread_pool_t>(1U);

    options_t options = TestType::options(db);

    testing::cleanup::file_t const flatnode_cleaner{options.flat_node_file};

    // create some nodes, ways, and relations we'll use for the tests
    test_buffer_t buffer;
    auto const &node10 = buffer.add_node("n10 x1.0 y0.0");
    auto const &node11 = buffer.add_node("n11 x1.1 y0.0");
    auto const &node12 = buffer.add_node("n12 x1.2 y0.0");
    auto const &node10a = buffer.add_node("n10 x1.0 y1.0");
    auto const &node11a = buffer.add_node("n11 x1.1 y1.0");

    auto const &node10d = buffer.add_node("n10 dD");
    auto const &node11d = buffer.add_node("n11 dD");

    auto const &way20 = buffer.add_way("w20 Nn11,n12");

    auto const &rel30 = buffer.add_relation("r30 Mn10@");
    auto const &rel31 = buffer.add_relation("r31 Mw20@");

    // Set up middle in "create" mode to get a cleanly initialized database and
    // add some nodes and ways. Does this in its own scope so that the mid is
    // closed properly.
    {
        auto mid = std::make_shared<middle_pgsql_t>(thread_pool, &options);
        mid->start();

        mid->node(node10);
        mid->node(node11);
        mid->node(node12);
        mid->after_nodes();
        mid->way(way20);
        mid->after_ways();
        mid->relation(rel30);
        mid->relation(rel31);
        mid->after_relations();

        mid->stop();
        mid->wait();
    }

    // From now on use append mode to not destroy the data we just added.
    options.append = true;

    SECTION("Single relation directly affected")
    {
        auto mid = std::make_shared<middle_pgsql_t>(thread_pool, &options);
        mid->start();

        auto output = std::make_shared<output_null_t>(mid->get_query_instance(),
                                                      thread_pool, options);
        osmdata_t osmdata{mid, output, options};

        osmdata.node(node10d);
        osmdata.node(node10a);
        osmdata.after_nodes();
        osmdata.after_ways();
        osmdata.after_relations();

        REQUIRE(osmdata.get_pending_way_ids().empty());
        idlist_t const &rel_ids = osmdata.get_pending_relation_ids();
        REQUIRE(rel_ids == idlist_t{30});

        check_relation(mid, rel30);
    }

    SECTION("Single relation indirectly affected (through way)")
    {
        auto mid = std::make_shared<middle_pgsql_t>(thread_pool, &options);
        mid->start();

        auto output = std::make_shared<output_null_t>(mid->get_query_instance(),
                                                      thread_pool, options);
        osmdata_t osmdata{mid, output, options};

        osmdata.node(node11d);
        osmdata.node(node11a);
        osmdata.after_nodes();
        osmdata.after_ways();
        osmdata.after_relations();

        idlist_t const &way_ids = osmdata.get_pending_way_ids();
        REQUIRE(way_ids == idlist_t{20});
        idlist_t const &rel_ids = osmdata.get_pending_relation_ids();
        REQUIRE(rel_ids == idlist_t{31});
        check_relation(mid, rel31);
    }
}
