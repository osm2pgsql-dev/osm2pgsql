/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "tile.hpp"

geom::point_t tile_t::to_tile_coords(geom::point_t p,
                                     unsigned int pixel_extent) const noexcept
{
    double const f = static_cast<double>(pixel_extent) / extent();
    return {(p.x() - xmin()) * f, (p.y() - ymin()) * f};
}

geom::point_t tile_t::to_world_coords(geom::point_t p,
                                      unsigned int pixel_extent) const noexcept
{
    double const f = extent() / static_cast<double>(pixel_extent);
    return {p.x() * f + xmin(), p.y() * f + ymin()};
}

geom::point_t tile_t::center() const noexcept
{
    return to_world_coords({0.5, 0.5}, 1);
}

// Quadkey implementation uses bit interleaving code from
// https://github.com/lemire/Code-used-on-Daniel-Lemire-s-blog/blob/master/2018/01/08/interleave.c

static uint64_t interleave_uint32_with_zeros(uint32_t input) noexcept
{
    uint64_t word = input;
    word = (word ^ (word << 16U)) & 0x0000ffff0000ffffULL;
    word = (word ^ (word << 8U)) & 0x00ff00ff00ff00ffULL;
    word = (word ^ (word << 4U)) & 0x0f0f0f0f0f0f0f0fULL;
    word = (word ^ (word << 2U)) & 0x3333333333333333ULL;
    word = (word ^ (word << 1U)) & 0x5555555555555555ULL;
    return word;
}

static uint32_t deinterleave_lowuint32(uint64_t word) noexcept
{
    word &= 0x5555555555555555ULL;
    word = (word ^ (word >> 1U)) & 0x3333333333333333ULL;
    word = (word ^ (word >> 2U)) & 0x0f0f0f0f0f0f0f0fULL;
    word = (word ^ (word >> 4U)) & 0x00ff00ff00ff00ffULL;
    word = (word ^ (word >> 8U)) & 0x0000ffff0000ffffULL;
    word = (word ^ (word >> 16U)) & 0x00000000ffffffffULL;
    return static_cast<uint32_t>(word);
}

uint64_t tile_t::quadkey() const noexcept
{
    return interleave_uint32_with_zeros(m_x) |
           (interleave_uint32_with_zeros(m_y) << 1U);
}

tile_t tile_t::from_quadkey(uint64_t quadkey, uint32_t zoom) noexcept
{
    return {zoom, deinterleave_lowuint32(quadkey),
            deinterleave_lowuint32(quadkey >> 1U)};
}
