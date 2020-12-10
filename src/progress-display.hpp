#ifndef OSM2PGSQL_PROGRESS_DISPLAY_HPP
#define OSM2PGSQL_PROGRESS_DISPLAY_HPP

/**
 * \file
 *
 * This file is part of osm2pgsql (https://github.com/openstreetmap/osm2pgsql).
 *
 * It contains the progress_display_t class.
 */

#include <cstddef>
#include <ctime>

#include <osmium/handler.hpp>

#include "osmtypes.hpp"

/**
 * The progress_display_t class is used to display how far the processing of
 * the input data has progressed.
 */
class progress_display_t : public osmium::handler::Handler
{
    struct Counter
    {
        std::size_t count = 0;
        osmid_t max = 0;
        std::time_t start = 0;

        osmid_t count_k() const noexcept { return count / 1000; }

        std::size_t add(osmid_t id) noexcept
        {
            max = id;
            return ++count;
        }
    };

public:
    progress_display_t(bool enabled = false) noexcept : m_enabled(enabled)
    {
        m_node.start = std::time(nullptr);
    }

    void node(osmium::Node const &node)
    {
        if (m_node.add(node.id()) % 10000 == 0) {
            possibly_print_status();
        }
    }

    void after_nodes() { m_way.start = std::time(nullptr); }

    void way(osmium::Way const &way)
    {
        if (m_way.add(way.id()) % 1000 == 0) {
            possibly_print_status();
        }
    }

    void after_ways() { m_rel.start = std::time(nullptr); }

    void relation(osmium::Relation const &relation)
    {
        if (m_rel.add(relation.id()) % 10 == 0) {
            possibly_print_status();
        }
    }

    void after_relations() const { print_status(std::time(nullptr)); }

    void print_summary() const;

private:
    void possibly_print_status();
    void print_status(std::time_t now) const;

    uint64_t nodes_time(std::time_t now) const noexcept
    {
        if (m_node.count == 0) {
            return 0;
        }
        return (m_way.start > 0 ? m_way.start : now) - m_node.start;
    }

    uint64_t ways_time(std::time_t now) const noexcept
    {
        if (m_way.count == 0) {
            return 0;
        }
        return (m_rel.start > 0 ? m_rel.start : now) - m_way.start;
    }

    uint64_t rels_time(std::time_t now) const noexcept
    {
        if (m_rel.count == 0) {
            return 0;
        }
        return now - m_rel.start;
    }

    Counter m_node{};
    Counter m_way{};
    Counter m_rel{};
    std::time_t m_last_print_time{std::time(nullptr)};
    bool m_enabled;
};

#endif // OSM2PGSQL_PROGRESS_DISPLAY_HPP
