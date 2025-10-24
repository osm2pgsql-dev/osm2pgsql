/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2026 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>

#include "expire-tiles.hpp"
#include "format.hpp"
#include "geom-functions.hpp"
#include "options.hpp"
#include "projection.hpp"
#include "reprojection.hpp"
#include "table.hpp"
#include "tile.hpp"
#include "wkb.hpp"

expire_tiles_t::expire_tiles_t(uint32_t max_zoom,
                               std::shared_ptr<reprojection_t> projection,
                               std::size_t max_tiles_geometry)
: m_projection(std::move(projection)), m_max_tiles_geometry(max_tiles_geometry),
  m_maxzoom(max_zoom), m_map_width(static_cast<int>(1U << m_maxzoom))
{
}

void expire_tiles_t::expire_tile(uint32_t x, uint32_t y)
{
    if (m_dirty_tiles.size() > m_max_tiles_geometry) {
        return;
    }

    tile_t const new_tile{m_maxzoom, x, y};
    m_dirty_tiles.insert(new_tile.quadkey());
}

void expire_tiles_t::commit_tiles(expire_output_t *expire_output)
{
    if (!expire_output || m_dirty_tiles.empty()) {
        return;
    }
    expire_output->add_tiles(m_dirty_tiles);
    m_dirty_tiles.clear();
}

uint32_t expire_tiles_t::normalise_tile_x_coord(int x) const
{
    x %= m_map_width;
    if (x < 0) {
        x = (m_map_width - x) + 1;
    }
    return static_cast<uint32_t>(x);
}

geom::point_t expire_tiles_t::coords_to_tile(geom::point_t const &point)
{
    auto const c = m_projection->target_to_tile(point);

    return {m_map_width * (0.5 + c.x() / tile_t::EARTH_CIRCUMFERENCE),
            m_map_width * (0.5 - c.y() / tile_t::EARTH_CIRCUMFERENCE)};
}

void expire_tiles_t::from_point_list(geom::point_list_t const &list,
                                     expire_config_t const &expire_config)
{
    for_each_segment(list, [&](geom::point_t const &a, geom::point_t const &b) {
        from_line_segment(a, b, expire_config);
    });
}

void expire_tiles_t::from_geometry(geom::point_t const &geom,
                                   expire_config_t const &expire_config)
{
    auto const tilec = coords_to_tile(geom);

    auto const ymin =
        std::max(0U, static_cast<uint32_t>(tilec.y() - expire_config.buffer));

    auto const ymax =
        std::min(m_map_width - 1U,
                 static_cast<uint32_t>(tilec.y() + expire_config.buffer));

    for (int x = static_cast<int>(tilec.x() - expire_config.buffer);
         x <= static_cast<int>(tilec.x() + expire_config.buffer); ++x) {
        uint32_t const norm_x = normalise_tile_x_coord(x);
        for (uint32_t y = ymin; y <= ymax; ++y) {
            expire_tile(norm_x, y);
        }
    }
}

void expire_tiles_t::from_geometry(geom::linestring_t const &geom,
                                   expire_config_t const &expire_config)
{
    from_point_list(geom, expire_config);
}

void expire_tiles_t::from_polygon_boundary(geom::polygon_t const &geom,
                                           expire_config_t const &expire_config)
{
    from_point_list(geom.outer(), expire_config);
    for (auto const &inner : geom.inners()) {
        from_point_list(inner, expire_config);
    }
}

namespace {

template <typename TGEOM>
expire_mode decide_expire_mode(TGEOM const &geom,
                               expire_config_t const &expire_config,
                               geom::box_t *box)
{
    if (expire_config.mode != expire_mode::hybrid) {
        return expire_config.mode;
    }

    *box = geom::envelope(geom);
    if (box->width() > expire_config.full_area_limit ||
        box->height() > expire_config.full_area_limit) {
        return expire_mode::boundary_only;
    }

    return expire_mode::full_area;
}

} // anonymous namespace

void expire_tiles_t::build_tile_list(std::vector<uint32_t> *tile_x_list,
                                     geom::ring_t const &ring, double tile_y)
{
    assert(!ring.empty());
    for (std::size_t i = 1; i < ring.size(); ++i) {
        auto const t1 = coords_to_tile(ring[i]);
        auto const t2 = coords_to_tile(ring[i - 1]);

        if ((t1.y() < tile_y && t2.y() >= tile_y) ||
            (t2.y() < tile_y && t1.y() >= tile_y)) {
            auto const pos =
                (tile_y - t1.y()) / (t2.y() - t1.y()) * (t2.x() - t1.x());
            tile_x_list->push_back(static_cast<uint32_t>(std::clamp(
                t1.x() + pos, 0.0, static_cast<double>(m_map_width - 1))));
        }
    }
}

void expire_tiles_t::from_polygon_area(geom::polygon_t const &geom,
                                       geom::box_t box)
{
    if (!box.valid()) {
        box = geom::envelope(geom);
    }

    // This uses a variation on a simple polygon fill algorithm, for instance
    // described on https://alienryderflex.com/polygon_fill/ . For each row
    // of tiles we find the intersections with the area boundary and "fill" in
    // the tiles between them. Note that we don't need to take particular care
    // about the boundary, because we simply use the algorithm we use for
    // expiry along a line to do that, which will also take care of the buffer.

    // Coordinates are numbered from bottom to top, tiles are numbered from top
    // to bottom, so "min" and "max" are switched here.
    auto const max_tile_y = static_cast<std::uint32_t>(
        m_map_width * (0.5 - box.min().y() / tile_t::EARTH_CIRCUMFERENCE));
    auto const min_tile_y = static_cast<std::uint32_t>(
        m_map_width * (0.5 - box.max().y() / tile_t::EARTH_CIRCUMFERENCE));

    std::vector<uint32_t> tile_x_list;

    //  Loop through the tile rows from top to bottom
    for (std::uint32_t tile_y = min_tile_y; tile_y < max_tile_y; ++tile_y) {

        // Build a list of tiles crossed by the area boundary
        tile_x_list.clear();
        build_tile_list(&tile_x_list, geom.outer(),
                        static_cast<double>(tile_y));
        for (auto const &ring : geom.inners()) {
            build_tile_list(&tile_x_list, ring, static_cast<double>(tile_y));
        }

        // Sort list of tiles from left to right
        std::sort(tile_x_list.begin(), tile_x_list.end());

        // Add the tiles between entering and leaving the area to expire list
        assert(tile_x_list.size() % 2 == 0);
        for (std::size_t i = 0; i < tile_x_list.size(); i += 2) {
            if (tile_x_list[i] >= static_cast<uint32_t>(m_map_width - 1)) {
                break;
            }
            if (tile_x_list[i + 1] > 0) {
                for (std::uint32_t tile_x = tile_x_list[i];
                     tile_x < tile_x_list[i + 1]; ++tile_x) {
                    expire_tile(tile_x, tile_y);
                }
            }
        }
    }
}

void expire_tiles_t::from_geometry(geom::polygon_t const &geom,
                                   expire_config_t const &expire_config)
{
    geom::box_t box;
    auto const mode = decide_expire_mode(geom, expire_config, &box);

    from_polygon_boundary(geom, expire_config);

    // Only need to expire area if in full_area mode. If there is only a
    // single tile expired the whole polygon is inside that tile and we
    // don't need to do the polygon expire.
    if (mode == expire_mode::full_area && m_dirty_tiles.size() > 1) {
        from_polygon_area(geom, box);
    }
}

void expire_tiles_t::from_geometry(geom::multipolygon_t const &geom,
                                   expire_config_t const &expire_config)
{
    geom::box_t box;
    auto const mode = decide_expire_mode(geom, expire_config, &box);

    for (auto const &sgeom : geom) {
        from_polygon_boundary(sgeom, expire_config);
    }

    // Only need to expire area if in full_area mode. If there is only a
    // single tile expired the whole polygon is inside that tile and we
    // don't need to do the polygon expire.
    if (mode == expire_mode::full_area && m_dirty_tiles.size() > 1) {
        for (auto const &sgeom : geom) {
            from_polygon_area(sgeom, geom::box_t{});
        }
    }
}

// False positive: Apparently clang-tidy can not see through the visit()
// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
void expire_tiles_t::from_geometry(geom::geometry_t const &geom,
                                   expire_config_t const &expire_config)
{
    if (!enabled()) {
        return;
    }

    geom.visit([&](auto const &g) { from_geometry(g, expire_config); });
}

void expire_tiles_t::from_geometry_if_3857(geom::geometry_t const &geom,
                                           expire_config_t const &expire_config)
{
    if (geom.srid() == PROJ_SPHERE_MERC) {
        from_geometry(geom, expire_config);
    }
}

/*
 * Expire tiles that a line crosses
 */
void expire_tiles_t::from_line_segment(geom::point_t const &a,
                                       geom::point_t const &b,
                                       expire_config_t const &expire_config)
{
    auto tilec_a = coords_to_tile(a);
    auto tilec_b = coords_to_tile(b);

    if (tilec_a.x() > tilec_b.x()) {
        /* We always want the line to go from left to right - swap the ends if it doesn't */
        std::swap(tilec_a, tilec_b);
    }

    double const x_len = tilec_b.x() - tilec_a.x();
    if (x_len > m_map_width / 2) { // NOLINT(bugprone-integer-division)
        /* If the line is wider than half the map, assume it
           crosses the international date line.
           These coordinates get normalised again later */
        tilec_a.set_x(tilec_a.x() + m_map_width);
        std::swap(tilec_a, tilec_b);
    }

    double const y_len = tilec_b.y() - tilec_a.y();
    double const hyp_len =
        std::sqrt(x_len * x_len + y_len * y_len); /* Pythagoras */
    double const x_step = x_len / hyp_len;
    double const y_step = y_len / hyp_len;

    for (int i = 0; i <= hyp_len / 0.4; ++i) {
        double const step = i * 0.4;
        double const next_step = std::min(hyp_len, (i + 1) * 0.4);

        double const x1 = tilec_a.x() + (step * x_step);
        double y1 = tilec_a.y() + (step * y_step);
        double const x2 = tilec_a.x() + (next_step * x_step);
        double y2 = tilec_a.y() + (next_step * y_step);

        /* The line (x1,y1),(x2,y2) is up to 1 tile width long
           x1 will always be <= x2
           We could be smart and figure out the exact tiles intersected,
           but for simplicity, treat the coordinates as a bounding box
           and expire everything within that box. */
        if (y1 > y2) {
            std::swap(y1, y2);
        }
        for (int x = static_cast<int>(x1 - expire_config.buffer);
             x <= static_cast<int>(x2 + expire_config.buffer); ++x) {
            uint32_t const norm_x = normalise_tile_x_coord(x);
            for (int y = static_cast<int>(y1 - expire_config.buffer);
                 y <= static_cast<int>(y2 + expire_config.buffer); ++y) {
                if (y >= 0) {
                    expire_tile(norm_x, static_cast<uint32_t>(y));
                }
            }
        }
    }
}

quadkey_list_t expire_tiles_t::get_tiles()
{
    quadkey_list_t tiles;
    tiles.reserve(m_dirty_tiles.size());
    tiles.assign(m_dirty_tiles.cbegin(), m_dirty_tiles.cend());
    std::sort(tiles.begin(), tiles.end());
    m_dirty_tiles.clear();
    return tiles;
}

int expire_from_result(expire_tiles_t *expire, pg_result_t const &result,
                       expire_config_t const &expire_config)
{
    auto const num_tuples = result.num_tuples();

    for (int i = 0; i < num_tuples; ++i) {
        expire->from_geometry(ewkb_to_geom(result.get(i, 0)), expire_config);
    }

    return num_tuples;
}
