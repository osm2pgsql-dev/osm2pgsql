/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "ordered-index.hpp"

#include <algorithm>
#include <cassert>

void ordered_index_t::add(osmid_t id, std::size_t offset)
{
    assert(m_ranges.empty() ||
           (last().to < id &&
            (last().offset_from + last().index.back().offset) < offset));

    if (need_new_2nd_level() ||
        (id - last().from) > std::numeric_limits<uint32_t>::max() ||
        (offset - last().offset_from) >= std::numeric_limits<uint32_t>::max()) {
        if (!m_ranges.empty()) {
            m_ranges.back().to = id - 1;
        }
        m_ranges.emplace_back(id, offset, m_block_size);
        m_capacity += m_block_size;
        if (m_block_size < max_block_size) {
            m_block_size <<= 1U;
        }
    }

    // Yes, the first second level block always contains {0, 0}. We
    // leave it that way to simplify the code.
    m_ranges.back().index.push_back(second_level_index_entry{
        static_cast<uint32_t>(id - last().from),
        static_cast<uint32_t>(offset - last().offset_from)});
    m_ranges.back().to = id;
    ++m_size;
}

std::pair<osmid_t, std::size_t> ordered_index_t::get_internal(osmid_t id) const
    noexcept
{
    if (m_ranges.empty()) {
        return {0, not_found_value()};
    }

    auto const rit = std::lower_bound(
        m_ranges.cbegin(), m_ranges.cend(), id,
        [](range_entry const &range, osmid_t id) { return range.to < id; });

    if (rit == m_ranges.end()) {
        return {last().from + last().index.back().id,
                last().offset_from + last().index.back().offset};
    }

    if (id < rit->from) {
        return {0, not_found_value()};
    }

    auto it = std::upper_bound(
        rit->index.cbegin(), rit->index.cend(), id - rit->from,
        [](std::size_t id, second_level_index_entry const &idx) {
            return id < idx.id;
        });
    assert(it != rit->index.cbegin());
    --it;

    return {rit->from + it->id, rit->offset_from + it->offset};
}
