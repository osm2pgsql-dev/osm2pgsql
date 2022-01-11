#ifndef OSM2PGSQL_PROGRESS_DISPLAY_HPP
#define OSM2PGSQL_PROGRESS_DISPLAY_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

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

/**
 * The progress_display_t class is used to display how far the processing of
 * the input data has progressed.
 */
class progress_display_t : public osmium::handler::Handler
{
    struct Counter
    {
        std::size_t count = 0;
        std::time_t start = 0;

        std::size_t count_k() const noexcept { return count / 1000; }
    };

public:
    explicit progress_display_t(bool enabled = false) noexcept
    : m_enabled(enabled)
    {
        m_node.start = std::time(nullptr);
    }

    void node(osmium::Node const &)
    {
        if (++m_node.count % 10000 == 0) {
            possibly_print_status();
        }
    }

    void way(osmium::Way const &)
    {
        if (++m_way.count % 1000 == 0) {
            possibly_print_status();
        }
    }


    void relation(osmium::Relation const &)
    {
        if (++m_rel.count % 10 == 0) {
            possibly_print_status();
        }
    }

    void start_way_counter() { m_way.start = std::time(nullptr); }

    void start_relation_counter() { m_rel.start = std::time(nullptr); }

    void print_summary() const;

private:
    void print_status(std::time_t now) const;
    void possibly_print_status();

    uint64_t nodes_time(std::time_t now) const noexcept;
    uint64_t ways_time(std::time_t now) const noexcept;
    uint64_t rels_time(std::time_t now) const noexcept;
    uint64_t overall_time(std::time_t now) const noexcept;

    Counter m_node{};
    Counter m_way{};
    Counter m_rel{};
    std::time_t m_last_print_time{std::time(nullptr)};
    bool m_enabled;
};

#endif // OSM2PGSQL_PROGRESS_DISPLAY_HPP
