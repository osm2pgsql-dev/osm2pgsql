#ifndef OSM2PGSQL_CANVAS_HPP
#define OSM2PGSQL_CANVAS_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "geom.hpp"
#include "tile.hpp"

#define cimg_display 0 // NOLINT(cppcoreguidelines-macro-usage)
#include "CImg.h"

#include <cstddef>

/**
 * This class wraps the image class from the CImg library.
 */
class canvas_t
{
public:
    static void info() { cimg_library::cimg::info(); }

    /**
     * Create a new image canvas. It will be quadratic and have the width and
     * height extent + 2*buffer.
     */
    canvas_t(std::size_t extent, std::size_t buffer)
    : m_extent(extent),
      m_buffer(buffer), m_rast{size(), size(), 1, 1, 0}, m_temp{size(), size(),
                                                                1, 1, 0}
    {}

    unsigned int size() const noexcept
    {
        return static_cast<unsigned int>(m_extent + 2 * m_buffer);
    }

    unsigned char const *begin() const noexcept { return m_rast.begin(); }
    unsigned char const *end() const noexcept { return m_rast.end(); }

    std::size_t draw(geom::geometry_t const &geometry, tile_t const &tile);

    unsigned char operator()(int x, int y) const noexcept
    {
        return m_rast(x, y, 0, 0);
    }

    void open_close(unsigned int buffer_size)
    {
        m_rast.dilate(buffer_size).erode(buffer_size * 2).dilate(buffer_size);
    }

    void save(std::string const &filename) const;

    std::string to_wkb(tile_t const &tile, double margin) const;

    void merge(canvas_t const &other);

private:
    constexpr static unsigned char const Black = 0;
    constexpr static unsigned char const White = 255;

    using image_type = cimg_library::CImg<unsigned char>;

    cimg_library::CImg<int> create_pointlist(geom::point_list_t const &pl,
                                             tile_t const &tile) const;

    std::size_t draw_polygon(geom::polygon_t const &polygon,
                             tile_t const &tile);

    std::size_t draw_linestring(geom::linestring_t const &linestring,
                                tile_t const &tile);

    std::size_t m_extent;
    std::size_t m_buffer;
    image_type m_rast;
    image_type m_temp;
}; // class canvas_t

std::string to_hex(std::string const &in);

#endif // OSM2PGSQL_CANVAS_HPP
