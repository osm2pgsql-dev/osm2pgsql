#include <cassert>
#include <iostream>

#include "node-persistent-cache.hpp"
#include "options.hpp"

#include "tests/common-cleanup.hpp"

#define FLAT_NODES_FILE_NAME "tests/test_middle_flat.flat.nodes.bin"

template <typename T>
void assert_equal(T actual, T expected)
{
    if (actual != expected) {
        std::cerr << "Expected " << expected << ", but got " << actual << ".\n";
        exit(1);
    }
}

void write_and_read_location(node_persistent_cache &cache, osmid_t id, double x,
                             double y)
{
    cache.set(id, osmium::Location(x, y));
    assert_equal(osmium::Location(x, y), cache.get(id));
}

void read_invalid_location(node_persistent_cache &cache, osmid_t id)
{
    assert_equal(osmium::Location(), cache.get(id));
}

void read_location(node_persistent_cache &cache, osmid_t id, double x, double y)
{
    assert_equal(osmium::Location(x, y), cache.get(id));
}

void delete_location(node_persistent_cache &cache, osmid_t id)
{
    cache.set(id, osmium::Location());
    assert_equal(osmium::Location(), cache.get(id));
}

void test_create()
{
    options_t options;
    options.flat_node_file = boost::optional<std::string>(FLAT_NODES_FILE_NAME);

    auto ram_cache = std::make_shared<node_ram_cache>(0, 0); // empty cache

    node_persistent_cache cache(&options, ram_cache);

    // write in order
    write_and_read_location(cache, 10, 10.01, -45.3);
    write_and_read_location(cache, 11, -0.4538, 22.22);
    write_and_read_location(cache, 1058, 9.4, 9);
    write_and_read_location(cache, 502754, 0.0, 0.0);

    // write out-of-order
    write_and_read_location(cache, 9934, -179.999, 89.1);

    // read non-existing in middle
    read_invalid_location(cache, 0);
    read_invalid_location(cache, 1111);
    read_invalid_location(cache, 1);

    // read non-existing after the last node
    read_invalid_location(cache, 502755);
    read_invalid_location(cache, 7772947204);
}

void test_append()
{
    options_t options;
    options.flat_node_file = boost::optional<std::string>(FLAT_NODES_FILE_NAME);

    auto ram_cache = std::make_shared<node_ram_cache>(0, 0); // empty cache

    node_persistent_cache cache(&options, ram_cache);

    // read all previously written locations
    read_location(cache, 10, 10.01, -45.3);
    read_location(cache, 11, -0.4538, 22.22);
    read_location(cache, 1058, 9.4, 9);
    read_location(cache, 502754, 0.0, 0.0);
    read_location(cache, 9934, -179.999, 89.1);

    // everything else should still be invalid
    read_invalid_location(cache, 0);
    read_invalid_location(cache, 12);
    read_invalid_location(cache, 1059);
    read_invalid_location(cache, 1);
    read_invalid_location(cache, 1057);
    read_invalid_location(cache, 502753);
    read_invalid_location(cache, 502755);
    read_invalid_location(cache, 77729404);

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
}

int main()
{
    cleanup::file flat_nodes_file(FLAT_NODES_FILE_NAME);

    test_create();
    test_append();
}
