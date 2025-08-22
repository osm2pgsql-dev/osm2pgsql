/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2025 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "tile.hpp"

#include "format.hpp"

#include <osmium/util/string.hpp>

#include <cstdlib>

std::string tile_t::to_zxy() const
{
    return fmt::format("{}/{}/{}", zoom(), x(), y());
}

geom::point_t tile_t::to_tile_coords(geom::point_t p,
                                     unsigned int pixel_extent) const noexcept
{
    double const factor = static_cast<double>(pixel_extent) / extent();
    return {(p.x() - xmin()) * factor, (p.y() - ymin()) * factor};
}

geom::point_t tile_t::to_world_coords(geom::point_t p,
                                      unsigned int pixel_extent) const noexcept
{
    double const factor = extent() / static_cast<double>(pixel_extent);
    return {p.x() * factor + xmin(), p.y() * factor + ymin()};
}

geom::point_t tile_t::center() const noexcept
{
    return to_world_coords({0.5, 0.5}, 1);
}

namespace {

// Quadkey implementation uses bit interleaving code from
// https://github.com/lemire/Code-used-on-Daniel-Lemire-s-blog/blob/master/2018/01/08/interleave.c

uint64_t interleave_uint32_with_zeros(uint32_t input) noexcept
{
    uint64_t word = input;
    word = (word ^ (word << 16U)) & 0x0000ffff0000ffffULL;
    word = (word ^ (word << 8U)) & 0x00ff00ff00ff00ffULL;
    word = (word ^ (word << 4U)) & 0x0f0f0f0f0f0f0f0fULL;
    word = (word ^ (word << 2U)) & 0x3333333333333333ULL;
    word = (word ^ (word << 1U)) & 0x5555555555555555ULL;
    return word;
}

uint32_t deinterleave_lowuint32(uint64_t word) noexcept
{
    word &= 0x5555555555555555ULL;
    word = (word ^ (word >> 1U)) & 0x3333333333333333ULL;
    word = (word ^ (word >> 2U)) & 0x0f0f0f0f0f0f0f0fULL;
    word = (word ^ (word >> 4U)) & 0x00ff00ff00ff00ffULL;
    word = (word ^ (word >> 8U)) & 0x0000ffff0000ffffULL;
    word = (word ^ (word >> 16U)) & 0x00000000ffffffffULL;
    return static_cast<uint32_t>(word);
}

uint32_t parse_num_with_max(std::string const &str, uint32_t max)
{
    std::size_t pos = 0;
    auto const value = std::stoul(str, &pos);
    if (pos != str.size()) {
        throw std::invalid_argument{"extra characters"};
    }
    if (value >= max) {
        throw std::invalid_argument{"value to large"};
    }
    return static_cast<uint32_t>(value);
}

} // anonymous namespace

quadkey_t tile_t::quadkey() const noexcept
{
    return quadkey_t{interleave_uint32_with_zeros(m_x) |
                     (interleave_uint32_with_zeros(m_y) << 1U)};
}

tile_t tile_t::from_quadkey(quadkey_t quadkey, uint32_t zoom) noexcept
{
    return {zoom, deinterleave_lowuint32(quadkey.value()),
            deinterleave_lowuint32(quadkey.value() >> 1U)};
}

tile_t tile_t::from_zxy(std::string const &zxy)
{
    auto const p = osmium::split_string(zxy, '/');
    if (p.size() != 3) {
        throw fmt_error("Invalid tile '{}'.", zxy);
    }

    auto const zoom = parse_num_with_max(p[0], MAX_ZOOM);
    uint32_t const max = 1UL << zoom;

    return {zoom, parse_num_with_max(p[1], max), parse_num_with_max(p[2], max)};
}
