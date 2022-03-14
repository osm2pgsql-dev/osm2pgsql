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

#include "geom.hpp"
#include "geom-box.hpp"
#include "logging.hpp"
#include "osmtypes.hpp"
#include "pgsql.hpp"
#include "tile.hpp"

class reprojection;

/**
 * Implementation of the output of the tile expiry list to a file.
 */
class tile_output_t
{
    FILE *outfile;

public:
    explicit tile_output_t(char const *filename);

    ~tile_output_t();

    /**
     * Output dirty tile.
     *
     * \param tile The tile to write out.
     */
    void output_dirty_tile(tile_t const &tile);
};

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
     *               geometries. (This is usally done using the "get_wkb"
     *               prepared statement.)
     * \param osm_id The OSM id to look for.
     * \return The number of elements that refer to the osm_id or -1 if
     *         expire is disabled.
     */
    int from_result(pg_result_t const &result, osmid_t osm_id);

    /**
     * Write the list of expired tiles to a file.
     *
     * You will probably use tile_output_t as template argument for production code
     * and another class which does not write to a file for unit tests.
     *
     * \param filename name of the file
     * \param minzoom minimum zoom level
     */
    void output_and_destroy(char const *filename, uint32_t minzoom);

    /**
     * Output expired tiles on all requested zoom levels.
     *
     * \tparam TILE_WRITER class which implements the method
     * output_dirty_tile(tile_t const &tile) which usually writes the tile ID
     * to a file (production code) or does something else (usually unit tests).
     *
     * \param minzoom minimum zoom level
     */
    template <class TILE_WRITER>
    void output_and_destroy(TILE_WRITER &output_writer, uint32_t minzoom)
    {
        assert(minzoom <= m_maxzoom);
        // build a sorted vector of all expired tiles
        std::vector<uint64_t> tiles_maxzoom(m_dirty_tiles.begin(),
                                            m_dirty_tiles.end());
        std::sort(tiles_maxzoom.begin(), tiles_maxzoom.end());
        /* Loop over all requested zoom levels (from maximum down to the minimum zoom level).
         * Tile IDs of the tiles enclosing this tile at lower zoom levels are calculated using
         * bit shifts.
         *
         * last_quadkey is initialized with a value which is not expected to exist
         * (larger than largest possible quadkey). */
        uint64_t last_quadkey = 1ULL << (2 * m_maxzoom);
        std::size_t count = 0;
        for (auto const quadkey : tiles_maxzoom) {
            for (uint32_t dz = 0; dz <= m_maxzoom - minzoom; ++dz) {
                // scale down to the current zoom level
                uint64_t qt_current = quadkey >> (dz * 2);
                /* If dz > 0, there are propably multiple elements whose quadkey
                 * is equal because they are all sub-tiles of the same tile at the current
                 * zoom level. We skip all of them after we have written the first sibling.
                 */
                if (qt_current == last_quadkey >> (dz * 2)) {
                    continue;
                }
                auto const tile =
                    tile_t::from_quadkey(qt_current, m_maxzoom - dz);
                output_writer.output_dirty_tile(tile);
                ++count;
            }
            last_quadkey = quadkey;
        }
        log_info("Wrote {} entries to expired tiles list", count);
    }

    /**
    * merge the list of expired tiles in the other object into this
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

#endif // OSM2PGSQL_EXPIRE_TILES_HPP
