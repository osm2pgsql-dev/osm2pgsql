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

#include <memory>
#include <string>
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
#include "reprojection.hpp"
#include "tile.hpp"

class reprojection;

class expire_tiles
{
public:
    expire_tiles(uint32_t max_zoom, std::shared_ptr<reprojection> projection);

    explicit expire_tiles(std::string name)
    : m_projection(reprojection::create_projection(3857)),
      m_name(std::move(name))
    {}

    std::string const &name() const noexcept { return m_name; }

    std::string filename() const noexcept { return m_filename; }

    void set_filename(std::string filename)
    {
        m_filename = std::move(filename);
    }

    std::string schema() const noexcept { return m_schema; }

    std::string table() const noexcept { return m_table; }

    void set_schema_and_table(std::string schema, std::string table)
    {
        m_schema = std::move(schema);
        m_table = std::move(table);
    }

    uint32_t minzoom() const noexcept { return m_minzoom; }
    void set_minzoom(uint32_t minzoom) noexcept { m_minzoom = minzoom; }

    uint32_t maxzoom() const noexcept { return m_maxzoom; }

    void set_maxzoom(uint32_t maxzoom) noexcept
    {
        m_maxzoom = maxzoom;
        m_map_width = 1U << m_maxzoom;
    }

    std::size_t output(std::string const &conninfo);

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

    /// The name of the tileset
    std::string m_name;

    /// The filename (if any) for output
    std::string m_filename;

    /// The schema (if any) for output
    std::string m_schema;

    /// The table (if any) for output
    std::string m_table;

    /// Minimum zoom level for output
    uint32_t m_minzoom = 0;

    /// Zoom level we capture tiles on
    uint32_t m_maxzoom = 0;

    int m_map_width = 0;

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

/**
 * Iterate over tiles and call output function for each tile on all requested
 * zoom levels.
 *
 * \tparam OUTPUT Class with operator() taking a tile_t argument
 *
 * \param tiles_at_maxzoom The list of tiles at maximum zoom level
 * \param minzoom Minimum zoom level
 * \param maxzoom Maximum zoom level
 * \param output Output function
 */
template <class OUTPUT>
std::size_t for_each_tile(quadkey_list_t const &tiles_at_maxzoom,
                          uint32_t minzoom, uint32_t maxzoom, OUTPUT &&output)
{
    assert(minzoom <= maxzoom);

    if (minzoom == maxzoom) {
        for (auto const quadkey : tiles_at_maxzoom) {
            std::forward<OUTPUT>(output)(
                tile_t::from_quadkey(quadkey, maxzoom));
        }
        return tiles_at_maxzoom.size();
    }

    /**
     * Loop over all requested zoom levels (from maximum down to the minimum
     * zoom level).
     */
    quadkey_t last_quadkey{};
    std::size_t count = 0;
    for (auto const quadkey : tiles_at_maxzoom) {
        for (uint32_t dz = 0; dz <= maxzoom - minzoom; ++dz) {
            auto const qt_current = quadkey.down(dz);
            /**
             * If dz > 0, there are probably multiple elements whose quadkey
             * is equal because they are all sub-tiles of the same tile at the
             * current zoom level. We skip all of them after we have written
             * the first sibling.
             */
            if (qt_current != last_quadkey.down(dz)) {
                std::forward<OUTPUT>(output)(
                    tile_t::from_quadkey(qt_current, maxzoom - dz));
                ++count;
            }
        }
        last_quadkey = quadkey;
    }
    return count;
}

/**
 * Write the list of tiles to a file.
 *
 * \param tiles_at_maxzoom The list of tiles at maximum zoom level
 * \param minzoom Minimum zoom level
 * \param maxzoom Maximum zoom level
 * \param filename Name of the file
 */
std::size_t output_tiles_to_file(quadkey_list_t const &tiles_at_maxzoom,
                                 uint32_t minzoom, uint32_t maxzoom,
                                 std::string_view filename);

/**
 * Write the list of tiles to a database table. The table will be created
 * if it doesn't exist already.
 *
 * \param tiles_at_maxzoom The list of tiles at maximum zoom level
 * \param minzoom Minimum zoom level
 * \param maxzoom Maximum zoom level
 * \param conninfo database connection info
 * \param schema The schema the table is in (empty for public schema)
 * \param table The table name
 */
std::size_t output_tiles_to_table(quadkey_list_t const &tiles_at_maxzoom,
                                  uint32_t minzoom, uint32_t maxzoom,
                                  std::string const &conninfo,
                                  std::string const &schema,
                                  std::string const &table);

#endif // OSM2PGSQL_EXPIRE_TILES_HPP
