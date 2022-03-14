/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

/*
 * Dirty tile list generation
 *
 * Please refer to the OpenPisteMap expire_tiles.py script for a demonstration
 * of how to make use of the output:
 * http://subversion.nexusuk.org/projects/openpistemap/trunk/scripts/expire_tiles.py
 */

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
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

tile_output_t::tile_output_t(char const *filename)
: outfile(fopen(filename, "a"))
{
    if (outfile == nullptr) {
        log_warn("Failed to open expired tiles file ({}).  Tile expiry "
                 "list will not be written!",
                 std::strerror(errno));
    }
}

tile_output_t::~tile_output_t()
{
    if (outfile) {
        fclose(outfile);
    }
}

void tile_output_t::output_dirty_tile(tile_t const &tile)
{
    if (!outfile) {
        return;
    }

    fmt::print(outfile, "{}/{}/{}\n", tile.zoom(), tile.x(), tile.y());
}

expire_tiles::expire_tiles(uint32_t max_zoom, double max_bbox,
                           std::shared_ptr<reprojection> projection)
: m_projection(std::move(projection)), m_max_bbox(max_bbox),
  m_maxzoom(max_zoom), m_map_width(1U << m_maxzoom)
{}

void expire_tiles::output_and_destroy(char const *filename, uint32_t minzoom)
{
    tile_output_t output_writer{filename};
    output_and_destroy<tile_output_t>(output_writer, minzoom);
}

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

void expire_tiles::from_geometry(geom::geometry_t const &geom, osmid_t osm_id)
{
    if (geom.srid() != 3857) {
        return;
    }

    if (geom.is_point()) {
        auto const box = geom::envelope(geom);
        from_bbox(box);
    } else if (geom.is_linestring()) {
        from_point_list(geom.get<geom::linestring_t>());
    } else if (geom.is_multilinestring()) {
        for (auto const &list : geom.get<geom::multilinestring_t>()) {
            from_point_list(list);
        }
    } else if (geom.is_polygon() || geom.is_multipolygon()) {
        auto const box = geom::envelope(geom);
        if (from_bbox(box)) {
            /* Bounding box too big - just expire tiles on the line */
            log_debug("Large polygon ({:.0f} x {:.0f} metres, OSM ID {})"
                      " - only expiring perimeter",
                      box.max_x() - box.min_x(), box.max_y() - box.min_y(), osm_id);
            if (geom.is_polygon()) {
                from_point_list(geom.get<geom::polygon_t>().outer());
                for (auto const &inner : geom.get<geom::polygon_t>().inners()) {
                    from_point_list(inner);
                }
            } else if (geom.is_multipolygon()) {
                for (auto const &polygon : geom.get<geom::multipolygon_t>()) {
                    from_point_list(polygon.outer());
                    for (auto const &inner : polygon.inners()) {
                        from_point_list(inner);
                    }
                }
            }
        }
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
            double const temp = y2;
            y2 = y1;
            y1 = temp;
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
    int min_tile_x = tmp_min.x() - tile_expiry_leeway;
    int min_tile_y = tmp_min.y() - tile_expiry_leeway;

    auto const tmp_max = coords_to_tile({box.max_x(), box.min_y()});
    int max_tile_x = tmp_max.x() + tile_expiry_leeway;
    int max_tile_y = tmp_max.y() + tile_expiry_leeway;

    if (min_tile_x < 0) {
        min_tile_x = 0;
    }
    if (min_tile_y < 0) {
        min_tile_y = 0;
    }
    if (max_tile_x > m_map_width) {
        max_tile_x = m_map_width;
    }
    if (max_tile_y > m_map_width) {
        max_tile_y = m_map_width;
    }
    for (int iterator_x = min_tile_x; iterator_x <= max_tile_x; ++iterator_x) {
        uint32_t const norm_x = normalise_tile_x_coord(iterator_x);
        for (int iterator_y = min_tile_y; iterator_y <= max_tile_y;
             ++iterator_y) {
            expire_tile(norm_x, static_cast<uint32_t>(iterator_y));
        }
    }
    return 0;
}

int expire_tiles::from_result(pg_result_t const &result, osmid_t osm_id)
{
    if (!enabled()) {
        return -1;
    }

    auto const num_tuples = result.num_tuples();
    for (int i = 0; i < num_tuples; ++i) {
        char const *const wkb = result.get_value(i, 0);
        from_geometry(ewkb_to_geom(decode_hex(wkb)), osm_id);
    }

    return num_tuples;
}

void expire_tiles::merge_and_destroy(expire_tiles *other)
{
    if (m_map_width != other->m_map_width) {
        throw std::runtime_error{"Unable to merge tile expiry sets when "
                                 "map_width does not match: {} != {}."_format(
                                     m_map_width, other->m_map_width)};
    }

    if (m_dirty_tiles.empty()) {
        m_dirty_tiles = std::move(other->m_dirty_tiles);
    } else {
        m_dirty_tiles.insert(other->m_dirty_tiles.begin(),
                             other->m_dirty_tiles.end());
        other->m_dirty_tiles.clear();
    }
}
