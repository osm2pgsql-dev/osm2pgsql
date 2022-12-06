/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

/*
 * Dirty tile list generation
 *
 * Please refer to the OpenPisteMap expire_tiles.py script for a demonstration
 * of how to make use of the output:
 * http://subversion.nexusuk.org/projects/openpistemap/trunk/scripts/expire_tiles.py
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
#include "logging.hpp"
#include "options.hpp"
#include "reprojection.hpp"
#include "table.hpp"
#include "tile.hpp"
#include "wkb.hpp"

// How many tiles worth of space to leave either side of a changed feature
static constexpr double const tile_expiry_leeway = 0.1;

expire_tiles::expire_tiles(uint32_t max_zoom, double max_bbox,
                           std::shared_ptr<reprojection> projection)
: m_projection(std::move(projection)), m_max_bbox(max_bbox),
  m_maxzoom(max_zoom), m_map_width(1U << m_maxzoom)
{}

void expire_tiles::expire_tile(uint32_t x, uint32_t y)
{
    // Only try to insert to tile into the set if the last inserted tile
    // is different from this tile.
    tile_t const new_tile{m_maxzoom, x, y};
    if (!m_prev_tile.valid() || m_prev_tile != new_tile) {
        m_dirty_tiles.insert(new_tile.quadkey());
        m_prev_tile = new_tile;
    }
}

uint32_t expire_tiles::normalise_tile_x_coord(int x) const
{
    x %= m_map_width;
    if (x < 0) {
        x = (m_map_width - x) + 1;
    }
    return static_cast<uint32_t>(x);
}

geom::point_t expire_tiles::coords_to_tile(geom::point_t const &point)
{
    auto const c = m_projection->target_to_tile(point);

    return {m_map_width * (0.5 + c.x() / tile_t::earth_circumference),
            m_map_width * (0.5 - c.y() / tile_t::earth_circumference)};
}

void expire_tiles::from_point_list(geom::point_list_t const &list)
{
    for_each_segment(list, [&](geom::point_t const &a, geom::point_t const &b) {
        from_line(a, b);
    });
}

void expire_tiles::from_geometry(geom::point_t const &geom)
{
    geom::box_t const box = geom::envelope(geom);
    from_bbox(box);
}

void expire_tiles::from_geometry(geom::linestring_t const &geom)
{
    from_point_list(geom);
}

void expire_tiles::from_polygon_boundary(geom::polygon_t const &geom)
{
    from_point_list(geom.outer());
    for (auto const &inner : geom.inners()) {
        from_point_list(inner);
    }
}

void expire_tiles::from_geometry(geom::polygon_t const &geom)
{
    geom::box_t const box = geom::envelope(geom);
    if (from_bbox(box)) {
        /* Bounding box too big - just expire tiles on the boundary */
        from_polygon_boundary(geom);
    }
}

void expire_tiles::from_geometry(geom::multipolygon_t const &geom)
{
    geom::box_t const box = geom::envelope(geom);
    if (from_bbox(box)) {
        /* Bounding box too big - just expire tiles on the boundary */
        for (auto const &sgeom : geom) {
            from_polygon_boundary(sgeom);
        }
    }
}

void expire_tiles::from_geometry(geom::geometry_t const &geom)
{
    geom.visit([&](auto const &g) { from_geometry(g); });
}

void expire_tiles::from_geometry_if_3857(geom::geometry_t const &geom)
{
    if (geom.srid() == 3857) {
        from_geometry(geom);
    }
}

/*
 * Expire tiles that a line crosses
 */
void expire_tiles::from_line(geom::point_t const &a, geom::point_t const &b)
{
    auto tilec_a = coords_to_tile(a);
    auto tilec_b = coords_to_tile(b);

    if (tilec_a.x() > tilec_b.x()) {
        /* We always want the line to go from left to right - swap the ends if it doesn't */
        std::swap(tilec_a, tilec_b);
    }

    double const x_len = tilec_b.x() - tilec_a.x();
    if (x_len > m_map_width / 2) {
        /* If the line is wider than half the map, assume it
           crosses the international date line.
           These coordinates get normalised again later */
        tilec_a.set_x(tilec_a.x() + m_map_width);
        std::swap(tilec_a, tilec_b);
    }

    double const y_len = tilec_b.y() - tilec_a.y();
    double const hyp_len = sqrt(pow(x_len, 2) + pow(y_len, 2)); /* Pythagoras */
    double const x_step = x_len / hyp_len;
    double const y_step = y_len / hyp_len;

    for (double step = 0; step <= hyp_len; step += 0.4) {
        /* Interpolate points 1 tile width apart */
        double next_step = step + 0.4;
        if (next_step > hyp_len) {
            next_step = hyp_len;
        }
        double x1 = tilec_a.x() + ((double)step * x_step);
        double y1 = tilec_a.y() + ((double)step * y_step);
        double x2 = tilec_a.x() + ((double)next_step * x_step);
        double y2 = tilec_a.y() + ((double)next_step * y_step);

        /* The line (x1,y1),(x2,y2) is up to 1 tile width long
           x1 will always be <= x2
           We could be smart and figure out the exact tiles intersected,
           but for simplicity, treat the coordinates as a bounding box
           and expire everything within that box. */
        if (y1 > y2) {
            std::swap(y1, y2);
        }
        for (int x = x1 - tile_expiry_leeway; x <= x2 + tile_expiry_leeway;
             ++x) {
            uint32_t const norm_x = normalise_tile_x_coord(x);
            for (int y = y1 - tile_expiry_leeway; y <= y2 + tile_expiry_leeway;
                 ++y) {
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
int expire_tiles::from_bbox(geom::box_t const &box)
{
    if (!enabled()) {
        return 0;
    }

    double const width = box.width();
    double const height = box.height();
    if (width > tile_t::half_earth_circumference + 1) {
        /* Over half the planet's width within the bounding box - assume the
           box crosses the international date line and split it into two boxes */
        int ret = from_bbox({-tile_t::half_earth_circumference, box.min_y(),
                             box.min_x(), box.max_y()});
        ret += from_bbox({box.max_x(), box.min_y(),
                          tile_t::half_earth_circumference, box.max_y()});
        return ret;
    }

    if (width > m_max_bbox || height > m_max_bbox) {
        return -1;
    }

    /* Convert the box's Mercator coordinates into tile coordinates */
    auto const tmp_min = coords_to_tile({box.min_x(), box.max_y()});
    int const min_tile_x =
        std::clamp(int(tmp_min.x() - tile_expiry_leeway), 0, m_map_width);
    int const min_tile_y =
        std::clamp(int(tmp_min.y() - tile_expiry_leeway), 0, m_map_width);

    auto const tmp_max = coords_to_tile({box.max_x(), box.min_y()});
    int const max_tile_x =
        std::clamp(int(tmp_max.x() + tile_expiry_leeway), 0, m_map_width);
    int const max_tile_y =
        std::clamp(int(tmp_max.y() + tile_expiry_leeway), 0, m_map_width);

    for (int iterator_x = min_tile_x; iterator_x <= max_tile_x; ++iterator_x) {
        uint32_t const norm_x = normalise_tile_x_coord(iterator_x);
        for (int iterator_y = min_tile_y; iterator_y <= max_tile_y;
             ++iterator_y) {
            expire_tile(norm_x, static_cast<uint32_t>(iterator_y));
        }
    }
    return 0;
}

quadkey_list_t expire_tiles::get_tiles()
{
    quadkey_list_t tiles;
    tiles.reserve(m_dirty_tiles.size());
    tiles.assign(m_dirty_tiles.begin(), m_dirty_tiles.end());
    std::sort(tiles.begin(), tiles.end());
    m_dirty_tiles.clear();
    return tiles;
}

void expire_tiles::merge_and_destroy(expire_tiles *other)
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
        m_dirty_tiles.insert(other->m_dirty_tiles.begin(),
                             other->m_dirty_tiles.end());
        other->m_dirty_tiles.clear();
    }
}

std::size_t output_tiles_to_file(quadkey_list_t const &tiles_maxzoom,
                                 char const *filename, uint32_t minzoom,
                                 uint32_t maxzoom)
{
    FILE *outfile = std::fopen(filename, "a");
    if (outfile == nullptr) {
        log_warn("Failed to open expired tiles file ({}).  Tile expiry "
                 "list will not be written!",
                 std::strerror(errno));
        return 0;
    }

    auto const count =
        for_each_tile(tiles_maxzoom, minzoom, maxzoom, [&](tile_t const &tile) {
            fmt::print(outfile, "{}/{}/{}\n", tile.zoom(), tile.x(), tile.y());
        });

    std::fclose(outfile);

    return count;
}

int expire_from_result(expire_tiles *expire, pg_result_t const &result)
{
    auto const num_tuples = result.num_tuples();

    for (int i = 0; i < num_tuples; ++i) {
        expire->from_geometry(ewkb_to_geom(result.get(i, 0)));
    }

    return num_tuples;
}
