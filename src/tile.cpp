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

uint64_t tile_t::quadkey() const noexcept
{
    uint64_t quadkey = 0;

    for (uint32_t z = 0; z < m_zoom; ++z) {
        quadkey |= ((m_x & (1ULL << z)) << z);
        quadkey |= ((m_y & (1ULL << z)) << (z + 1));
    }

    return quadkey;
}

tile_t tile_t::from_quadkey(uint64_t quadkey, uint32_t zoom) noexcept
{
    uint32_t x = 0;
    uint32_t y = 0;
    for (uint32_t z = zoom; z > 0; --z) {
        y += static_cast<uint32_t>((quadkey & (1ULL << (2 * z - 1))) >> z);
        x += static_cast<uint32_t>((quadkey & (1ULL << (2 * (z - 1)))) >>
                                   (z - 1));
    }
    return {zoom, x, y};
}
