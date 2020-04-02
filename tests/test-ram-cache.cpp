#include <catch.hpp>

#include <vector>

#include "node-ram-cache.hpp"

enum class strategy_sparse
{
    value = ALLOC_SPARSE
};
enum class strategy_dense
{
    value = ALLOC_DENSE
};
enum class strategy_sparse_dense
{
    value = ALLOC_SPARSE | ALLOC_DENSE
};
enum class strategy_dense_chunk
{
    value = ALLOC_DENSE | ALLOC_DENSE_CHUNK
};
enum class strategy_sparse_dense_chunk
{
    value = ALLOC_SPARSE | ALLOC_DENSE | ALLOC_DENSE_CHUNK
};

static constexpr double test_lon(osmid_t id) noexcept { return 1 + 1e-5 * id; }

static void check_node(node_ram_cache *cache, osmid_t id, double x, double y)
{
    auto const node = cache->get(id);
    REQUIRE(node.valid());
    REQUIRE(node.lon() == Approx(x));
    REQUIRE(node.lat() == Approx(y));
}

TEMPLATE_TEST_CASE("Ram cache strict", "[NoDB]", strategy_sparse,
                   strategy_dense, strategy_sparse_dense, strategy_dense_chunk,
                   strategy_sparse_dense_chunk)
{
    node_ram_cache cache{(int)TestType::value, 10};

    std::vector<osmid_t> stored_nodes;

    // 2 dense blocks, the second partially filled at the start
    for (osmid_t id = 0; id < (node_ram_cache::per_block() +
                               (node_ram_cache::per_block() >> 1) + 1);
         ++id) {
        stored_nodes.push_back(id);
        cache.set(id, osmium::Location{test_lon(id), 0.0});
    }

    // 1 dense block, 75% filled
    for (osmid_t id = node_ram_cache::per_block() * 2;
         id < node_ram_cache::per_block() * 3; ++id) {
        if (id % 4 != 0) {
            stored_nodes.push_back(id);
            cache.set(id, osmium::Location{test_lon(id), 0.0});
        }
    }

    // 1 dense block, 20%
    for (osmid_t id = node_ram_cache::per_block() * 3;
         id < node_ram_cache::per_block() * 4; ++id) {
        if (id % 5 == 0) {
            stored_nodes.push_back(id);
            cache.set(id, osmium::Location{test_lon(id), 0.0});
        }
    }

    // A lone sparse node
    osmid_t const lone_id = node_ram_cache::per_block() * 5;
    stored_nodes.push_back(lone_id);
    cache.set(lone_id, osmium::Location{test_lon(lone_id), 0.0});

    // Now read everything back
    for (osmid_t id : stored_nodes) {
        check_node(&cache, id, test_lon(id), 0.0);
    }
}

TEMPLATE_TEST_CASE("Unordered node not allowed", "[NoDB]", strategy_sparse)
{
    SECTION("Strict cache")
    {
        node_ram_cache cache{(int)TestType::value, 10};

        cache.set(node_ram_cache::per_block() + 2, osmium::Location{4.0, 9.3});
        REQUIRE_THROWS(cache.set(25, osmium::Location(-4.0, -9.3)));
    }

    SECTION("Lossy cache")
    {
        node_ram_cache cache{(int)TestType::value | ALLOC_LOSSY, 10};

        cache.set(node_ram_cache::per_block() + 2, osmium::Location{4.0, 9.3});
        REQUIRE_NOTHROW(cache.set(25, osmium::Location(-4.0, -9.3)));
    }
}

TEMPLATE_TEST_CASE("Unordered node allowed", "[NoDB]", strategy_dense,
                   strategy_sparse_dense, strategy_dense_chunk,
                   strategy_sparse_dense_chunk)
{
    SECTION("Strict cache")
    {
        node_ram_cache cache{(int)TestType::value, 10};

        cache.set(node_ram_cache::per_block() + 2, osmium::Location{4.0, 9.3});
        cache.set(25, osmium::Location{-4.0, -9.3});
        check_node(&cache, 25, -4.0, -9.3);
    }

    SECTION("Lossy cache")
    {
        node_ram_cache cache{(int)TestType::value | ALLOC_LOSSY, 10};

        cache.set(node_ram_cache::per_block() + 2, osmium::Location{4.0, 9.3});
        cache.set(25, osmium::Location{-4.0, -9.3});
        check_node(&cache, 25, -4.0, -9.3);
    }
}
