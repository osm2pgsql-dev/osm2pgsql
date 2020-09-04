#ifndef OSM2PGSQL_MIDDLE_RAM_HPP
#define OSM2PGSQL_MIDDLE_RAM_HPP

/* Implements the mid-layer processing for osm2pgsql
 * using data structures in RAM.
 *
 * This layer stores data read in from the planet.osm file
 * and is then read by the backend processing code to
 * emit the final geometry-enabled output formats
*/

#include <array>
#include <memory>
#include <vector>

#include "middle.hpp"

class node_ram_cache;
class options_t;

template <typename T, size_t N>
class cache_block_t
{
    std::array<std::unique_ptr<T>, N> m_arr;

public:
    void set(size_t idx, T *ele) noexcept { m_arr[idx].reset(ele); }

    T const *get(size_t idx) const noexcept { return m_arr[idx].get(); }
};

template <typename T, size_t BLOCK_SHIFT>
class elem_cache_t
{
    constexpr static size_t per_block() noexcept { return 1ULL << BLOCK_SHIFT; }

    constexpr static size_t num_blocks() noexcept
    {
        return 1ULL << (32U - BLOCK_SHIFT);
    }

    constexpr static size_t id2block(osmid_t id) noexcept
    {
        /* + NUM_BLOCKS/2 allows for negative IDs */
        return (static_cast<size_t>(id) >> BLOCK_SHIFT) + num_blocks() / 2;
    }

    constexpr static size_t id2offset(osmid_t id) noexcept
    {
        return static_cast<size_t>(id) & (per_block() - 1U);
    }

    using element_t = cache_block_t<T, 1U << BLOCK_SHIFT>;

    std::vector<std::unique_ptr<element_t>> m_blocks;

public:
    elem_cache_t() : m_blocks(num_blocks()) {}

    void set(osmid_t id, T *ele)
    {
        size_t const block = id2block(id);
        assert(block < m_blocks.size());

        if (!m_blocks[block]) {
            m_blocks[block].reset(new element_t{});
        }

        m_blocks[block]->set(id2offset(id), ele);
    }

    T const *get(osmid_t id) const
    {
        size_t const block = id2block(id);
        assert(block < m_blocks.size());

        if (!m_blocks[block]) {
            return nullptr;
        }

        return m_blocks[block]->get(id2offset(id));
    }

    void clear()
    {
        for (auto &ele : m_blocks) {
            ele.reset();
        }
    }
};

struct middle_ram_t : public middle_t, public middle_query_t
{
    explicit middle_ram_t(options_t const *options);
    ~middle_ram_t() noexcept override = default;

    void start() override {}
    void stop(thread_pool_t &pool) override;
    void analyze() override {}
    void commit() override {}

    void node_set(osmium::Node const &node) override;
    size_t nodes_get_list(osmium::WayNodeList *nodes) const override;

    void way_set(osmium::Way const &way) override;
    bool way_get(osmid_t id, osmium::memory::Buffer &buffer) const override;
    size_t rel_way_members_get(osmium::Relation const &rel, rolelist_t *roles,
                               osmium::memory::Buffer &buffer) const override;

    bool relation_get(osmid_t id,
                      osmium::memory::Buffer &buffer) const override;
    void relation_set(osmium::Relation const &rel) override;

    void flush() override {}

    std::shared_ptr<middle_query_t> get_query_instance() override;

private:
    struct ramWay
    {
        taglist_t tags;
        idlist_t ndids;

        ramWay(osmium::Way const &way, bool add_attributes)
        : tags(way.tags()), ndids(way.nodes())
        {
            if (add_attributes) {
                tags.add_attributes(way);
            }
        }
    };

    struct ramRel
    {
        taglist_t tags;
        memberlist_t members;

        ramRel(osmium::Relation const &rel, bool add_attributes)
        : tags(rel.tags()), members(rel.members())
        {
            if (add_attributes) {
                tags.add_attributes(rel);
            }
        }
    };

    elem_cache_t<ramWay, 10> m_ways;
    elem_cache_t<ramRel, 10> m_rels;

    std::unique_ptr<node_ram_cache> m_cache;
    bool m_extra_attributes;
};

#endif // OSM2PGSQL_MIDDLE_RAM_HPP
