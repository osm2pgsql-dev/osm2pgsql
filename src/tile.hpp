#ifndef OSM2PGSQL_TILE_HPP
#define OSM2PGSQL_TILE_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "geom.hpp"

#include <cstdint>
#include <limits>

/**
 * A tile in the usual web tile format.
 */
class tile_t
{
public:
    static constexpr double const earth_circumference = 40075016.68;
    static constexpr double const half_earth_circumference =
        earth_circumference / 2;

    /// Construct an invalid tile.
    tile_t() noexcept = default;

    /**
     * Construct a new tile object.
     *
     * \pre \code zoom < 32 \endcode
     * \pre \code x < (1 << zoom) \endcode
     * \pre \code y < (1 << zoom) \endcode
     */
    tile_t(uint32_t zoom, uint32_t x, uint32_t y) noexcept
    : m_x(x), m_y(y), m_zoom(zoom)
    {
        assert(m_zoom < max_zoom);
        assert(x < (1UL << m_zoom));
        assert(y < (1UL << m_zoom));
    }

    uint32_t zoom() const noexcept
    {
        assert(valid());
        return m_zoom;
    }

    uint32_t x() const noexcept
    {
        assert(valid());
        return m_x;
    }

    uint32_t y() const noexcept
    {
        assert(valid());
        return m_y;
    }

    bool valid() const noexcept { return m_zoom != invalid_zoom; }

    /// The width/height of the tile in web mercator (EPSG:3857) coordinates.
    double extent() const noexcept
    {
        return earth_circumference / static_cast<double>(1UL << m_zoom);
    }

    /// Minimum X coordinate of this tile in web mercator (EPSG:3857) units.
    double xmin() const noexcept
    {
        return -half_earth_circumference + m_x * extent();
    }

    /// Maximum X coordinate of this tile in web mercator (EPSG:3857) units.
    double xmax() const noexcept
    {
        return -half_earth_circumference + (m_x + 1) * extent();
    }

    /// Minimum Y coordinate of this tile in web mercator (EPSG:3857) units.
    double ymin() const noexcept
    {
        return half_earth_circumference - (m_y + 1) * extent();
    }

    /// Maximum Y coordinate of this tile in web mercator (EPSG:3857) units.
    double ymax() const noexcept
    {
        return half_earth_circumference - m_y * extent();
    }

    /**
     * Convert a point from web mercator (EPSG:3857) coordinates to
     * coordinates in the tile assuming a tile extent of `pixel_extent`.
     */
    geom::point_t to_tile_coords(geom::point_t p,
                                 unsigned int pixel_extent) const noexcept;

    /**
     * Convert from tile coordinates (assuming a tile extent of `pixel_extent`
     * to web mercator (EPSG:3857) coordinates.
     */
    geom::point_t to_world_coords(geom::point_t p,
                                  unsigned int pixel_extent) const noexcept;

    /// The center of this tile in web mercator (EPSG:3857) units.
    geom::point_t center() const noexcept;

    /// Tiles are equal if x, y, and zoom are equal.
    friend bool operator==(tile_t const &a, tile_t const &b) noexcept
    {
        return (a.m_x == b.m_x) && (a.m_y == b.m_y) && (a.m_zoom == b.m_zoom);
    }

    friend bool operator!=(tile_t const &a, tile_t const &b) noexcept
    {
        return !(a == b);
    }

    /// Compare two tiles by the zoom, x, and y coordinates (in that order).
    friend bool operator<(tile_t const &a, tile_t const &b) noexcept
    {
        if (a.m_zoom < b.m_zoom) {
            return true;
        }
        if (a.m_zoom > b.m_zoom) {
            return false;
        }
        if (a.m_x < b.m_x) {
            return true;
        }
        if (a.m_x > b.m_x) {
            return false;
        }
        if (a.m_y < b.m_y) {
            return true;
        }

        return false;
    }

    /**
     * Return quadkey for this tile. The quadkey contains the interleaved
     * bits from the x and y values, similar to what's used for Bing maps:
     * https://docs.microsoft.com/en-us/bingmaps/articles/bing-maps-tile-system
     */
    uint64_t quadkey() const noexcept;

    /**
     * Construct tile from quadkey.
     */
    static tile_t from_quadkey(uint64_t quadkey, uint32_t zoom) noexcept;

private:
    static constexpr uint32_t const invalid_zoom =
        std::numeric_limits<uint32_t>::max();
    static constexpr uint32_t const max_zoom = 32;

    uint32_t m_x = 0;
    uint32_t m_y = 0;
    uint32_t m_zoom = invalid_zoom;
}; // class tile_t

#endif // OSM2PGSQL_TILE_HPP
