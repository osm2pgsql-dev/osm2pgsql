/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2021 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "node-locations.hpp"

#include <osmium/osm/location.hpp>
#include <osmium/util/delta.hpp>

// Workaround: This must be included before buffer_string.hpp due to a missing
// include in the upstream code. https://github.com/mapbox/protozero/pull/104
#include <protozero/config.hpp>

#include <protozero/buffer_string.hpp>
#include <protozero/varint.hpp>

#include <cassert>
#include <limits>

void node_locations_t::set(osmid_t id, osmium::Location location)
{
    assert(block_index() == 0 || m_block[block_index() - 1].first < id);

    m_block[block_index()] = {id, location};
    ++m_count;
    if (block_index() == 0) {
        freeze();
    }
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
    std::size_t num = block_size;
    for (std::size_t n = 0; n < block_size; ++n) {
        auto bid = did.update(protozero::decode_varint(&begin, end));
        if (bid == id) {
            num = n;
        }
        if (bid > id && num == block_size) {
            return osmium::Location{};
        }
    }
    if (num == block_size) {
        return osmium::Location{};
    }

    osmium::DeltaDecode<int64_t> dx;
    osmium::DeltaDecode<int64_t> dy;
    int32_t x = 0;
    int32_t y = 0;
    for (std::size_t n = 0; n <= num; ++n) {
        x = dx.update(
            protozero::decode_zigzag64(protozero::decode_varint(&begin, end)));
        y = dy.update(
            protozero::decode_zigzag64(protozero::decode_varint(&begin, end)));
    }

    return osmium::Location{x, y};
}

void node_locations_t::freeze()
{
    encode_block();
    clear_block();
}

void node_locations_t::clear()
{
    m_data.clear();
    m_data.shrink_to_fit();
    m_index.clear();
    clear_block();
    m_count = 0;
}

void node_locations_t::encode_block()
{
    auto const offset = m_data.size();
    osmium::DeltaEncode<osmid_t> did;
    osmium::DeltaEncode<int64_t> dx;
    osmium::DeltaEncode<int64_t> dy;
    for (auto const &nl : m_block) {
        protozero::add_varint_to_buffer(&m_data, did.update(nl.first));
    }
    for (auto const &nl : m_block) {
        protozero::add_varint_to_buffer(
            &m_data, protozero::encode_zigzag64(dx.update(nl.second.x())));
        protozero::add_varint_to_buffer(
            &m_data, protozero::encode_zigzag64(dy.update(nl.second.y())));
    }
    m_index.add(m_block[0].first, offset);
}

void node_locations_t::clear_block()
{
    for (auto &nl : m_block) {
        nl.first = std::numeric_limits<osmid_t>::max();
        nl.second = osmium::Location{};
    }
}

