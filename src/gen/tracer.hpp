#ifndef OSM2PGSQL_TRACER_HPP
#define OSM2PGSQL_TRACER_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "canvas.hpp"
#include "geom.hpp"
#include "tile.hpp"

#include <potracelib.h>

#include <memory>
#include <vector>

class tracer_t
{
public:
    tracer_t(std::size_t extent, std::size_t buffer, int turdsize)
    : m_param(potrace_param_default()), m_extent(extent), m_buffer(buffer)
    {
        m_param->alphamax = 0.0;
        m_param->turdsize = turdsize;
    }

    std::vector<geom::geometry_t>
    trace(canvas_t const &canvas, tile_t const &tile, double min_area = 0.0);

    void reset();

    std::size_t num_points() const noexcept { return m_num_points; }

private:
    static constexpr auto const bits_per_word = sizeof(potrace_word) * 8;

    geom::point_t make_point(potrace_dpoint_t const &p) const noexcept;

    struct potrace_param_deleter
    {
        void operator()(potrace_param_t *ptr) const noexcept
        {
            potrace_param_free(ptr);
        }
    };

    struct potrace_state_deleter
    {
        void operator()(potrace_state_t *ptr) const noexcept
        {
            potrace_state_free(ptr);
        }
    };

    void prepare(canvas_t const &canvas) noexcept;

    std::vector<geom::geometry_t> build_geometries(tile_t const &tile,
                                                   potrace_path_t const *plist,
                                                   double min_area) noexcept;

    std::vector<potrace_word> m_bits;
    potrace_bitmap_t m_bitmap{};
    std::unique_ptr<potrace_param_t, potrace_param_deleter> m_param;
    std::unique_ptr<potrace_state_t, potrace_state_deleter> m_state;
    std::size_t m_extent;
    std::size_t m_buffer;
    std::size_t m_num_points = 0;

}; // class tracer_t

#endif // OSM2PGSQL_TRACER_HPP
