/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2025 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "node-locations.hpp"

#include "logging.hpp"

// Workaround: This must be included before buffer_string.hpp due to a missing
// include in the upstream code. https://github.com/mapbox/protozero/pull/104
#include <protozero/config.hpp>

#include <protozero/buffer_string.hpp>
#include <protozero/varint.hpp>

#include <cassert>

bool node_locations_t::set(osmid_t id, osmium::Location location)
{
    if (used_memory() >= m_max_size && will_resize()) {
        return false;
    }

    if (first_entry_in_block()) {
        m_did.clear();
        m_dx.clear();
        m_dy.clear();
        m_index.add(id, m_data.size());
    }

    auto const delta = m_did.update(id);
    // Always true because ids in input must be unique and ordered
    assert(delta > 0);
    protozero::add_varint_to_buffer(&m_data, static_cast<uint64_t>(delta));

    protozero::add_varint_to_buffer(
        &m_data, protozero::encode_zigzag64(m_dx.update(location.x())));
    protozero::add_varint_to_buffer(
        &m_data, protozero::encode_zigzag64(m_dy.update(location.y())));

    ++m_count;

    return true;
}

osmium::Location node_locations_t::get(osmid_t id) const
{
    auto const offset = m_index.get_block(id);
    if (offset == ordered_index_t::not_found_value()) {
        return osmium::Location{};
    }

    assert(offset < m_data.size());

    char const *begin = m_data.data() + offset;
    char const *const end = m_data.data() + m_data.size();

    osmium::DeltaDecode<osmid_t> did;
    osmium::DeltaDecode<int64_t> dx;
    osmium::DeltaDecode<int64_t> dy;

    for (std::size_t n = 0; n < BLOCK_SIZE && begin != end; ++n) {
        auto const bid = did.update(
            static_cast<int64_t>(protozero::decode_varint(&begin, end)));
        auto const x = static_cast<int32_t>(dx.update(
            protozero::decode_zigzag64(protozero::decode_varint(&begin, end))));
        auto const y = static_cast<int32_t>(dy.update(
            protozero::decode_zigzag64(protozero::decode_varint(&begin, end))));
        if (bid == id) {
            return osmium::Location{x, y};
        }
        if (bid > id) {
            break;
        }
    }
    return osmium::Location{};
}

void node_locations_t::log_stats()
{
    constexpr auto MBYTE = 1024 * 1024;
    log_debug("Node locations cache:");
    log_debug("  num locations stored: {}", m_count);
    log_debug("  bytes overall: {}MB", used_memory() / MBYTE);
    log_debug("  data capacity: {}MB", m_data.capacity() / MBYTE);
    log_debug("  data size: {}MB", m_data.size() / MBYTE);
    log_debug("  index used memory: {}MB", m_index.used_memory() / MBYTE);
}

void node_locations_t::clear()
{
    m_data.clear();
    m_data.shrink_to_fit();
    m_index.clear();
    m_count = 0;
}
