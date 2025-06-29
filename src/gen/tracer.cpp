/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2025 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "tracer.hpp"

#include "canvas.hpp"
#include "geom-boost-adaptor.hpp"
#include "projection.hpp"
#include "tile.hpp"

#include <cassert>
#include <cmath>
#include <stdexcept>

static_assert(sizeof(potrace_word) == 8);

namespace {

potrace_word bit_squeeze(potrace_word w, unsigned char const *d) noexcept
{
    return (0x80U & d[0]) | (0x40U & d[1]) | (0x20U & d[2]) | (0x10U & d[3]) |
           (0x08U & d[4]) | (0x04U & d[5]) | (0x02U & d[6]) | (0x01U & d[7]) |
           w;
}

} // anonymous namespace

geom::point_t tracer_t::make_point(potrace_dpoint_t const &p) const noexcept
{
    return {p.x - static_cast<double>(m_buffer),
            static_cast<double>(m_extent + m_buffer) - p.y};
}

std::vector<geom::geometry_t>
tracer_t::trace(canvas_t const &canvas, tile_t const &tile, double min_area)
{
    prepare(canvas);

    potrace_bitmap_t const bitmap{int(canvas.size()), int(canvas.size()),
                                  int(canvas.size() / BITS_PER_WORD),
                                  m_bits.data()};

    std::unique_ptr<potrace_state_t, potrace_state_deleter> state{
        potrace_trace(m_param.get(), &bitmap)};

    if (!state || state->status != POTRACE_STATUS_OK) {
        throw std::runtime_error{"potrace failed"};
    }

    return build_geometries(tile, state->plist, min_area);
}

void tracer_t::reset()
{
    m_bits.clear();
    m_num_points = 0;
}

void tracer_t::prepare(canvas_t const &canvas) noexcept
{
    std::size_t const size = canvas.size();
    assert(size % BITS_PER_WORD == 0);

    m_bits.reserve((size * size) / BITS_PER_WORD);

    for (unsigned char const *d = canvas.begin(); d != canvas.end(); d += 8) {
        auto w = bit_squeeze(0, d);

        w = bit_squeeze(w << 8U, d += 8);
        w = bit_squeeze(w << 8U, d += 8);
        w = bit_squeeze(w << 8U, d += 8);
        w = bit_squeeze(w << 8U, d += 8);
        w = bit_squeeze(w << 8U, d += 8);
        w = bit_squeeze(w << 8U, d += 8);
        w = bit_squeeze(w << 8U, d += 8);

        m_bits.push_back(w);
    }
}

std::vector<geom::geometry_t>
tracer_t::build_geometries(tile_t const &tile, potrace_path_t const *plist,
                           double min_area) noexcept
{
    std::vector<geom::geometry_t> geometries;
    if (!plist) {
        return geometries;
    }

    for (potrace_path_t const *path = plist; path != nullptr;
         path = path->next) {

        geom::ring_t ring;

        auto const n = path->curve.n;
        assert(path->curve.tag[n - 1] == POTRACE_CORNER);
        ring.push_back(tile.to_world_coords(make_point(path->curve.c[n - 1][2]),
                                            m_extent));
        for (int i = 0; i < n; ++i) {
            assert(path->curve.tag[i] == POTRACE_CORNER);
            auto const &c = path->curve.c[i];
            ring.push_back(tile.to_world_coords(make_point(c[1]), m_extent));
            ring.push_back(tile.to_world_coords(make_point(c[2]), m_extent));
        }

        auto const ring_area =
            std::abs(static_cast<double>(boost::geometry::area(ring)));
        if (ring_area >= min_area) {
            m_num_points += ring.size();

            if (path->sign == '+') {
                geometries.emplace_back(geom::polygon_t{}, PROJ_SPHERE_MERC)
                    .get<geom::polygon_t>()
                    .outer() = std::move(ring);
            } else {
                assert(!geometries.empty());
                geometries.back().get<geom::polygon_t>().add_inner_ring(
                    std::move(ring));
            }
        }
    }

    return geometries;
}
