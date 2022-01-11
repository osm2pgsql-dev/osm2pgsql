/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "format.hpp"
#include "logging.hpp"
#include "progress-display.hpp"
#include "util.hpp"

static double count_per_second(std::size_t count, uint64_t elapsed) noexcept
{
    if (count == 0) {
        return 0.0;
    }

    if (elapsed == 0) {
        return count;
    }

    return static_cast<double>(count) / elapsed;
}

static std::string cps_display(std::size_t count, uint64_t elapsed)
{
    double const cps = count_per_second(count, elapsed);

    if (cps >= 1000.0) {
        return "{:.0f}k/s"_format(cps / 1000);
    }
    return "{:.0f}/s"_format(cps);
}

void progress_display_t::print_summary() const
{
    std::time_t const now = std::time(nullptr);

    if (m_enabled) {
        get_logger().no_leading_return();
        fmt::print(stderr, "\r{:90s}\r", "");
    }

    log_info("Reading input files done in {}.",
             util::human_readable_duration(overall_time(now)));

    auto const nt = nodes_time(now);
    log_info("  Processed {} nodes in {} - {}", m_node.count,
             util::human_readable_duration(nt), cps_display(m_node.count, nt));

    auto const wt = ways_time(now);
    log_info("  Processed {} ways in {} - {}", m_way.count,
             util::human_readable_duration(wt), cps_display(m_way.count, wt));

    auto const rt = rels_time(now);
    log_info("  Processed {} relations in {} - {}", m_rel.count,
             util::human_readable_duration(rt), cps_display(m_rel.count, rt));
}

void progress_display_t::print_status(std::time_t now) const
{
    if (m_enabled) {
        get_logger().needs_leading_return();
        fmt::print(stderr,
                   "\rProcessing: Node({}k {:.1f}k/s) Way({}k {:.2f}k/s)"
                   " Relation({} {:.1f}/s)",
                   m_node.count_k(),
                   count_per_second(m_node.count_k(), nodes_time(now)),
                   m_way.count_k(),
                   count_per_second(m_way.count_k(), ways_time(now)),
                   m_rel.count, count_per_second(m_rel.count, rels_time(now)));
    }
}

void progress_display_t::possibly_print_status()
{
    std::time_t const now = std::time(nullptr);

    if (m_last_print_time < now) {
        m_last_print_time = now;
        print_status(now);
    }
}

uint64_t progress_display_t::nodes_time(std::time_t now) const noexcept
{
    if (m_node.count == 0) {
        return 0;
    }
    return static_cast<uint64_t>((m_way.start > 0 ? m_way.start : now) -
                                 m_node.start);
}

uint64_t progress_display_t::ways_time(std::time_t now) const noexcept
{
    if (m_way.count == 0) {
        return 0;
    }
    return static_cast<uint64_t>((m_rel.start > 0 ? m_rel.start : now) -
                                 m_way.start);
}

uint64_t progress_display_t::rels_time(std::time_t now) const noexcept
{
    if (m_rel.count == 0) {
        return 0;
    }
    return static_cast<uint64_t>(now - m_rel.start);
}

uint64_t progress_display_t::overall_time(std::time_t now) const noexcept
{
    return static_cast<uint64_t>(now - m_node.start);
}

