/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "tracer.hpp"

#include "geom-boost-adaptor.hpp"

#include <cassert>
#include <cmath>
#include <stdexcept>

geom::point_t tracer_t::make_point(potrace_dpoint_t const &p) const noexcept
{
    return {p.x - static_cast<double>(m_buffer),
            static_cast<double>(m_extent + m_buffer) - p.y};
}

std::vector<geom::geometry_t>
tracer_t::trace(canvas_t const &canvas, tile_t const &tile, double min_area)
{
    prepare(canvas);

    m_state.reset(potrace_trace(m_param.get(), &m_bitmap));
    if (!m_state || m_state->status != POTRACE_STATUS_OK) {
        throw std::runtime_error{"potrace failed"};
    }

    return build_geometries(tile, m_state->plist, min_area);
}

void tracer_t::reset()
{
    m_bits.clear();
    m_state.reset();
    m_num_points = 0;
}

void tracer_t::prepare(canvas_t const &canvas) noexcept
{
    std::size_t const size = canvas.size();
    assert(size % bits_per_word == 0);

    m_bits.reserve((size * size) / bits_per_word);

    unsigned char const *d = canvas.begin();
    while (d != canvas.end()) {
        potrace_word w = 0x1U & *d++;
        for (std::size_t n = 1; n < bits_per_word; ++n) {
            w <<= 1U;
            assert(d != canvas.end());
            w |= 0x1U & *d++;
        }
        m_bits.push_back(w);
    }

    m_bitmap = {int(size), int(size), int(size / bits_per_word), m_bits.data()};
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
                geometries.emplace_back(geom::polygon_t{}, 3857)
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
