#ifndef OSM2PGSQL_PROGRESS_DISPLAY_HPP
#define OSM2PGSQL_PROGRESS_DISPLAY_HPP

/**
 * \file
 *
 * This file is part of osm2pgsql (https://github.com/openstreetmap/osm2pgsql).
 *
 * It contains the progress_display_t class.
 */

#include <ctime>

#include "osmtypes.hpp"

/**
 * The progress_display_t class is used to display how far the processing of
 * the input data has progressed.
 */
class progress_display_t
{
    struct Counter
    {
        osmid_t count = 0;
        osmid_t max = 0;
        std::time_t start = 0;
        int m_frac;

        explicit Counter(int frac) noexcept : m_frac(frac) {}

        osmid_t count_k() const noexcept { return count / 1000; }

        bool add(osmid_t id) noexcept
        {
            if (id > max) {
                max = id;
            }
            if (count == 0) {
                start = std::time(nullptr);
            }
            ++count;

            return count % m_frac == 0;
        }

        Counter &operator+=(Counter const &rhs) noexcept
        {
            count += rhs.count;
            if (rhs.max > max) {
                max = rhs.max;
            }
            if (start == 0) {
                start = rhs.start;
            }

            return *this;
        }
    };

public:
    progress_display_t() noexcept : m_last_print_time(std::time(nullptr)) {}

    void update(progress_display_t const &other) noexcept;
    void print_summary() const;
    void print_status(std::time_t now) const;
    void possibly_print_status();

    void add_node(osmid_t id)
    {
        if (m_node.add(id)) {
            possibly_print_status();
        }
    }

    void add_way(osmid_t id)
    {
        if (m_way.add(id)) {
            possibly_print_status();
        }
    }

    void add_rel(osmid_t id)
    {
        if (m_rel.add(id)) {
            possibly_print_status();
        }
    }

private:
    static double count_per_second(osmid_t count, uint64_t elapsed) noexcept
    {
        if (count == 0) {
            return 0.0;
        }

        if (elapsed == 0) {
            return count;
        }

        return static_cast<double>(count) / elapsed;
    }

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

    Counter m_node{10000};
    Counter m_way{1000};
    Counter m_rel{10};
    std::time_t m_last_print_time;
};

#endif // OSM2PGSQL_PROGRESS_DISPLAY_HPP
