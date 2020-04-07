#include <catch.hpp>

#include "node-persistent-cache.hpp"
#include "options.hpp"

#include "common-cleanup.hpp"
#include "common-options.hpp"

static void write_and_read_location(node_persistent_cache &cache, osmid_t id,
                                    double x, double y)
{
    cache.set(id, osmium::Location{x, y});
    REQUIRE(osmium::Location(x, y) == cache.get(id));
}

static void read_location(node_persistent_cache const &cache, osmid_t id,
                          double x, double y)
{
    REQUIRE(osmium::Location(x, y) == cache.get(id));
}

static void delete_location(node_persistent_cache &cache, osmid_t id)
{
    cache.set(id, osmium::Location{});
    REQUIRE(osmium::Location{} == cache.get(id));
}

TEST_CASE("Persistent cache", "[NoDB]")
{
    options_t const options = testing::opt_t().flatnodes();
    testing::cleanup::file_t flatnode_cleaner{options.flat_node_file.get()};
    auto ram_cache = std::make_shared<node_ram_cache>(); // dummy cache

    // create a new cache
    {
        node_persistent_cache cache{&options, ram_cache};

        // write in order
        write_and_read_location(cache, 10, 10.01, -45.3);
        write_and_read_location(cache, 11, -0.4538, 22.22);
        write_and_read_location(cache, 1058, 9.4, 9);
        write_and_read_location(cache, 502754, 0.0, 0.0);

        // write out-of-order
        write_and_read_location(cache, 9934, -179.999, 89.1);

        // read non-existing in middle
        REQUIRE(cache.get(0) == osmium::Location{});
        REQUIRE(cache.get(1111) == osmium::Location{});
        REQUIRE(cache.get(1) == osmium::Location{});

        // read non-existing after the last node
        REQUIRE(cache.get(502755) == osmium::Location{});
        REQUIRE(cache.get(7772947204) == osmium::Location{});
    }

    // reopen the cache
    {
        node_persistent_cache cache{&options, ram_cache};

        // read all previously written locations
        read_location(cache, 10, 10.01, -45.3);
        read_location(cache, 11, -0.4538, 22.22);
        read_location(cache, 1058, 9.4, 9);
        read_location(cache, 502754, 0.0, 0.0);
        read_location(cache, 9934, -179.999, 89.1);

        // everything else should still be invalid
        REQUIRE(cache.get(0) == osmium::Location{});
        REQUIRE(cache.get(12) == osmium::Location{});
        REQUIRE(cache.get(1059) == osmium::Location{});
        REQUIRE(cache.get(1) == osmium::Location{});
        REQUIRE(cache.get(1057) == osmium::Location{});
        REQUIRE(cache.get(502753) == osmium::Location{});
        REQUIRE(cache.get(502755) == osmium::Location{});
        REQUIRE(cache.get(77729404) == osmium::Location{});

        // write new data in the middle
        write_and_read_location(cache, 13, 10.01, -45.3);
        write_and_read_location(cache, 3000, 45, 45);

        // append new data
        write_and_read_location(cache, 502755, 87, 0.45);
        write_and_read_location(cache, 502756, 87.12, 0.46);
        write_and_read_location(cache, 510000, 44, 0.0);

        // delete existing
        delete_location(cache, 11);

        // delete non-existing
        delete_location(cache, 21);

        // non-deleted should still be there
        read_location(cache, 10, 10.01, -45.3);
        read_location(cache, 1058, 9.4, 9);
        read_location(cache, 502754, 0.0, 0.0);
        read_location(cache, 9934, -179.999, 89.1);
    }
}
