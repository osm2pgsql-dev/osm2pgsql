#ifndef OSM2PGSQL_EXPIRE_TILES_HPP
#define OSM2PGSQL_EXPIRE_TILES_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <cstdint>
#include <memory>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "expire-config.hpp"
#include "geom.hpp"
#include "geom-box.hpp"
#include "logging.hpp"
#include "osmtypes.hpp"
#include "pgsql.hpp"
#include "tile.hpp"

class reprojection;

class expire_tiles
{
public:
    expire_tiles(uint32_t max_zoom, std::shared_ptr<reprojection> projection);

    bool empty() const noexcept { return m_dirty_tiles.empty(); }

    bool enabled() const noexcept { return m_maxzoom != 0; }

    void from_polygon_boundary(geom::polygon_t const &geom,
                               expire_config_t const &expire_config);

    void from_polygon_boundary(geom::multipolygon_t const &geom,
                               expire_config_t const &expire_config);

    void from_geometry(geom::nullgeom_t const & /*geom*/,
                       expire_config_t const & /*expire_config*/)
    {}

    void from_geometry(geom::point_t const &geom,
                       expire_config_t const &expire_config);

    void from_geometry(geom::linestring_t const &geom,
                       expire_config_t const &expire_config);

    void from_geometry(geom::polygon_t const &geom,
                       expire_config_t const &expire_config);

    void from_geometry(geom::multipolygon_t const &geom,
                       expire_config_t const &expire_config);

    template <typename T>
    void from_geometry(geom::multigeometry_t<T> const &geom,
                       expire_config_t const &expire_config)
    {
        for (auto const &sgeom : geom) {
            from_geometry(sgeom, expire_config);
        }
    }

    void from_geometry(geom::geometry_t const &geom,
                       expire_config_t const &expire_config);

    void from_geometry_if_3857(geom::geometry_t const &geom,
                               expire_config_t const &expire_config);

    int from_bbox(geom::box_t const &box, expire_config_t const &expire_config);

    /**
     * Get tiles as a vector of quadkeys and remove them from the expire_tiles
     * object.
     */
    quadkey_list_t get_tiles();

    /**
     * Merge the list of expired tiles in the other object into this
     * object, destroying the list in the other object.
     */
    void merge_and_destroy(expire_tiles *other);

private:
    /**
     * Converts from target coordinates to tile coordinates.
     */
    geom::point_t coords_to_tile(geom::point_t const &point);

    /**
     * Expire a single tile.
     *
     * \param x x index of the tile to be expired.
     * \param y y index of the tile to be expired.
     */
    void expire_tile(uint32_t x, uint32_t y);

    uint32_t normalise_tile_x_coord(int x) const;

    void from_line_segment(geom::point_t const &a, geom::point_t const &b,
                           expire_config_t const &expire_config);

    void from_point_list(geom::point_list_t const &list,
                         expire_config_t const &expire_config);

    /// This is where we collect all the expired tiles.
    std::unordered_set<quadkey_t> m_dirty_tiles;

    /// The tile which has been added last to the unordered set.
    tile_t m_prev_tile;

    std::shared_ptr<reprojection> m_projection;

    uint32_t m_maxzoom;
    int m_map_width;

}; // class expire_tiles

/**
 * Expire tiles based on an osm id.
 *
 * \param expire Where to mark expired tiles.
 * \param result Result of a database query into some table returning the
 *               geometries. (This is usually done using the "get_wkb"
 *               prepared statement.)
 * \param expire_config Configuration for expiry.
 * \return The number of tuples in the result or -1 if expire is disabled.
 */
int expire_from_result(expire_tiles *expire, pg_result_t const &result,
                       expire_config_t const &expire_config);

#endif // OSM2PGSQL_EXPIRE_TILES_HPP
