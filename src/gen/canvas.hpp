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

#include <opencv2/core.hpp>

#include <cstddef>

/**
 * This class wraps the image class from the OpenCV library.
 */
class canvas_t
{
public:
    /**
     * Create a new image canvas. It will be quadratic and have the width and
     * height extent + 2*buffer.
     */
    canvas_t(std::size_t extent, std::size_t buffer)
    : m_extent(extent), m_buffer(buffer), m_rast{static_cast<int>(size()),
                                                 static_cast<int>(size()),
                                                 CV_8UC1, cv::Scalar::all(0)}
    {
    }

    unsigned int size() const noexcept
    {
        return static_cast<unsigned int>(m_extent + 2 * m_buffer);
    }

    unsigned char const *begin() const noexcept { return m_rast.data; }

    unsigned char const *end() const noexcept
    {
        return m_rast.data + (static_cast<size_t>(size() * size()));
    }

    std::size_t draw(geom::geometry_t const &geometry, tile_t const &tile);

    void open_close(unsigned int buffer_size);

    void save(std::string const &filename) const;

    std::string to_wkb(tile_t const &tile, double margin) const;

    void merge(canvas_t const &other);

private:
    using image_type = cv::Mat;

    void create_pointlist(std::vector<cv::Point> *out,
                          geom::point_list_t const &pl,
                          tile_t const &tile) const;

    std::size_t draw_polygon(geom::polygon_t const &polygon,
                             tile_t const &tile);

    std::size_t draw_linestring(geom::linestring_t const &linestring,
                                tile_t const &tile);

    std::size_t m_extent;
    std::size_t m_buffer;
    image_type m_rast;
}; // class canvas_t

std::string to_hex(std::string const &in);

#endif // OSM2PGSQL_CANVAS_HPP
