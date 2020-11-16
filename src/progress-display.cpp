#include "format.hpp"
#include "logging.hpp"
#include "progress-display.hpp"

void progress_display_t::update(progress_display_t const &other) noexcept
{
    m_node += other.m_node;
    m_way += other.m_way;
    m_rel += other.m_rel;
}

void progress_display_t::print_summary() const
{
    std::time_t const now = std::time(nullptr);

    log_info("Node stats: total({}), max({}) in {}s", m_node.count, m_node.max,
             nodes_time(now));
    log_info("Way stats: total({}), max({}) in {}s", m_way.count, m_way.max,
             ways_time(now));
    log_info("Relation stats: total({}), max({}) in {}s", m_rel.count,
             m_rel.max, rels_time(now));
}

void progress_display_t::print_status(std::time_t now) const
{
    if (get_logger().show_progress()) {
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

    if (m_last_print_time >= now) {
        return;
    }
    m_last_print_time = now;

    print_status(now);
}
