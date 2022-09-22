#ifndef OSM2PGSQL_EXPIRE_TILES_HPP
#define OSM2PGSQL_EXPIRE_TILES_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <memory>
#include <unordered_set>
#include <utility>
#include <vector>

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
    expire_tiles(uint32_t max_zoom, double max_bbox,
                 std::shared_ptr<reprojection> projection);

    bool enabled() const noexcept { return m_maxzoom != 0; }

    void from_geometry(geom::geometry_t const &geom, osmid_t osm_id);

    int from_bbox(geom::box_t const &box);

    /**
     * Expire tiles based on an osm id.
     *
     * \param result Result of a database query into some table returning the
     *               geometries. (This is usually done using the "get_wkb"
     *               prepared statement.)
     * \param osm_id The OSM id to look for.
     * \return The number of elements that refer to the osm_id or -1 if
     *         expire is disabled.
     */
    int from_result(pg_result_t const &result, osmid_t osm_id);

    /**
     * Get tiles as a vector of quadkeys and remove them from the expire_tiles
     * object.
     */
    std::vector<uint64_t> get_tiles();

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
    void from_line(geom::point_t const &a, geom::point_t const &b);

    void from_point_list(geom::point_list_t const &list);

    /// This is where we collect all the expired tiles.
    std::unordered_set<uint64_t> m_dirty_tiles;

    /// The tile which has been added last to the unordered set.
    tile_t m_prev_tile;

    std::shared_ptr<reprojection> m_projection;

    double m_max_bbox;
    uint32_t m_maxzoom;
    int m_map_width;
};

/**
 * Iterate over tiles and call output function for each tile on all requested
 * zoom levels.
 *
 * \tparam OUTPUT Class with operator() taking a tile_t argument
 *
 * \param tiles The list of tiles at maximum zoom level
 * \param minzoom Minimum zoom level
 * \param maxzoom Maximum zoom level
 * \param output Output function
 */
template <class OUTPUT>
std::size_t for_each_tile(std::vector<uint64_t> const &tiles, uint32_t minzoom,
                          uint32_t maxzoom, OUTPUT &&output)
{
    assert(minzoom <= maxzoom);

    if (minzoom == maxzoom) {
        for (auto const quadkey : tiles) {
            std::forward<OUTPUT>(output)(
                tile_t::from_quadkey(quadkey, maxzoom));
        }
        return tiles.size();
    }

    /**
     * Loop over all requested zoom levels (from maximum down to the minimum
     * zoom level). Tile IDs of the tiles enclosing this tile at lower zoom
     * levels are calculated using bit shifts.
     *
     * last_quadkey is initialized with a value which is not expected to exist
     * (larger than largest possible quadkey).
     */
    uint64_t last_quadkey = 1ULL << (2 * maxzoom);
    std::size_t count = 0;
    for (auto const quadkey : tiles) {
        for (uint32_t dz = 0; dz <= maxzoom - minzoom; ++dz) {
            // scale down to the current zoom level
            uint64_t const qt_current = quadkey >> (dz * 2);
            /**
             * If dz > 0, there are probably multiple elements whose quadkey
             * is equal because they are all sub-tiles of the same tile at the
             * current zoom level. We skip all of them after we have written
             * the first sibling.
             */
            if (qt_current == last_quadkey >> (dz * 2)) {
                continue;
            }
            auto const tile = tile_t::from_quadkey(qt_current, maxzoom - dz);
            std::forward<OUTPUT>(output)(tile);
            ++count;
        }
        last_quadkey = quadkey;
    }
    return count;
}

/**
 * Write the list of tiles to a file.
 *
 * \param tiles The list of tiles at maximum zoom level
 * \param filename Name of the file
 * \param minzoom Minimum zoom level
 * \param maxzoom Maximum zoom level
 */
std::size_t output_tiles_to_file(std::vector<uint64_t> const &tiles,
                                 char const *filename, uint32_t minzoom,
                                 uint32_t maxzoom);

#endif // OSM2PGSQL_EXPIRE_TILES_HPP
