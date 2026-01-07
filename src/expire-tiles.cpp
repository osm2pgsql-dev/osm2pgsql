/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2025 by the osm2pgsql developer community.
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
                               std::shared_ptr<reprojection_t> projection)
: m_projection(std::move(projection)), m_maxzoom(max_zoom),
  m_map_width(static_cast<int>(1U << m_maxzoom))
{}

void expire_tiles_t::expire_tile(uint32_t x, uint32_t y)
{
    // Only try to insert to tile into the set if the last inserted tile
    // is different from this tile.
    tile_t const new_tile{m_maxzoom, x, y};
    if (!m_prev_tile.valid() || m_prev_tile != new_tile) {
        m_dirty_tiles.insert(new_tile.quadkey());
        m_prev_tile = new_tile;
    }
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

void expire_tiles_t::from_geometry(geom::polygon_t const &geom,
                                   expire_config_t const &expire_config)
{
    if (expire_config.mode == expire_mode::boundary_only) {
        from_polygon_boundary(geom, expire_config);
        return;
    }

    geom::box_t const box = geom::envelope(geom);
    if (from_bbox(box, expire_config)) {
        /* Bounding box too big - just expire tiles on the boundary */
        from_polygon_boundary(geom, expire_config);
    }
}

void expire_tiles_t::from_polygon_boundary(geom::multipolygon_t const &geom,
                                           expire_config_t const &expire_config)
{
    for (auto const &sgeom : geom) {
        from_polygon_boundary(sgeom, expire_config);
    }
}

void expire_tiles_t::from_geometry(geom::multipolygon_t const &geom,
                                   expire_config_t const &expire_config)
{
    if (expire_config.mode == expire_mode::boundary_only) {
        from_polygon_boundary(geom, expire_config);
        return;
    }

    geom::box_t const box = geom::envelope(geom);
    if (from_bbox(box, expire_config)) {
        /* Bounding box too big - just expire tiles on the boundary */
        from_polygon_boundary(geom, expire_config);
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

/*
 * Expire tiles within a bounding box
 */
int expire_tiles_t::from_bbox(geom::box_t const &box,
                              expire_config_t const &expire_config)
{
    double const width = box.width();
    double const height = box.height();

    if (expire_config.mode == expire_mode::hybrid &&
        (width > expire_config.full_area_limit ||
         height > expire_config.full_area_limit)) {
        return -1;
    }

    /* Convert the box's Mercator coordinates into tile coordinates */
    auto const tmp_min = coords_to_tile({box.min_x(), box.max_y()});
    int const min_tile_x =
        std::clamp(int(tmp_min.x() - expire_config.buffer), 0, m_map_width - 1);
    int const min_tile_y =
        std::clamp(int(tmp_min.y() - expire_config.buffer), 0, m_map_width - 1);

    auto const tmp_max = coords_to_tile({box.max_x(), box.min_y()});
    int const max_tile_x =
        std::clamp(int(tmp_max.x() + expire_config.buffer), 0, m_map_width - 1);
    int const max_tile_y =
        std::clamp(int(tmp_max.y() + expire_config.buffer), 0, m_map_width - 1);

    for (int iterator_x = min_tile_x; iterator_x <= max_tile_x; ++iterator_x) {
        uint32_t const norm_x = normalise_tile_x_coord(iterator_x);
        for (int iterator_y = min_tile_y; iterator_y <= max_tile_y;
             ++iterator_y) {
            expire_tile(norm_x, static_cast<uint32_t>(iterator_y));
        }
    }
    return 0;
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

void expire_tiles_t::merge_and_destroy(expire_tiles_t *other)
{
    if (m_map_width != other->m_map_width) {
        throw fmt_error("Unable to merge tile expiry sets when "
                        "map_width does not match: {} != {}.",
                        m_map_width, other->m_map_width);
    }

    if (m_dirty_tiles.empty()) {
        using std::swap;
        swap(m_dirty_tiles, other->m_dirty_tiles);
    } else {
        m_dirty_tiles.insert(other->m_dirty_tiles.cbegin(),
                             other->m_dirty_tiles.cend());
        other->m_dirty_tiles.clear();
    }
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
