/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "canvas.hpp"
#include "raster.hpp"

cimg_library::CImg<int> canvas_t::create_pointlist(geom::point_list_t const &pl,
                                                   tile_t const &tile) const
{
    cimg_library::CImg<int> points{static_cast<unsigned int>(pl.size()), 2};

    int n = 0;
    for (auto const point : pl) {
        auto const tp = tile.to_tile_coords(point, m_extent);
        points(n, 0) = static_cast<int>(static_cast<double>(m_buffer) + tp.x());
        points(n, 1) =
            static_cast<int>(static_cast<double>(m_buffer + m_extent) - tp.y());
        ++n;
    }

    return points;
}

std::size_t canvas_t::draw_polygon(geom::polygon_t const &polygon,
                                   tile_t const &tile)
{
    if (polygon.inners().empty()) {
        m_rast.draw_polygon(create_pointlist(polygon.outer(), tile), &White);
        return polygon.outer().size();
    }

    std::size_t num_points = polygon.outer().size();
    m_temp.draw_polygon(create_pointlist(polygon.outer(), tile), &White);
    for (auto const &inner : polygon.inners()) {
        num_points += inner.size();
        m_temp.draw_polygon(create_pointlist(inner, tile), &Black);
    }
    m_rast |= m_temp;

    return num_points;
}

std::size_t canvas_t::draw_linestring(geom::linestring_t const &linestring,
                                      tile_t const &tile)
{
    m_rast.draw_line(create_pointlist(linestring, tile), &White);
    return linestring.size();
}

std::size_t canvas_t::draw(geom::geometry_t const &geometry, tile_t const &tile)
{
    if (geometry.is_linestring()) {
        auto const &linestring = geometry.get<geom::linestring_t>();
        return draw_linestring(linestring, tile);
    }

    if (geometry.is_polygon()) {
        auto const &polygon = geometry.get<geom::polygon_t>();
        return draw_polygon(polygon, tile);
    }

    if (geometry.is_multipolygon()) {
        auto const &mp = geometry.get<geom::multipolygon_t>();
        std::size_t num_points = 0;
        for (auto const &p : mp) {
            num_points += draw_polygon(p, tile);
        }
        return num_points;
    }

    // XXX other geometry types?

    return 0;
}

void canvas_t::save(std::string const &filename) const
{
    m_rast.save(filename.c_str());
}

std::string canvas_t::to_wkb(tile_t const &tile, double margin) const
{
    std::string wkb;
    wkb.reserve(61 + 2 + m_rast.size());

    // header
    wkb_raster_header header{};
    header.nBands = 1;
    header.scaleX = tile.extent() / static_cast<double>(m_extent);
    header.scaleY = -header.scaleX;
    header.ipX = tile.xmin(margin);
    header.ipY = tile.ymax(margin);
    header.width = m_extent + 2 * m_buffer;
    header.height = header.width;
    add_raster_header(&wkb, header);

    // band
    wkb_raster_band band{};
    band.bits = 4;
    add_raster_band(&wkb, band);

    // rasterdata
    wkb.append(reinterpret_cast<char const *>(m_rast.data()), m_rast.size());

    assert(wkb.size() == 61 + 2 + m_rast.size());

    return wkb;
}

void canvas_t::merge(canvas_t const &other) { m_rast |= other.m_rast; }

std::string to_hex(std::string const &in)
{
    std::string result;
    char const *const lookup_hex = "0123456789ABCDEF";

    for (const auto c : in) {
        unsigned int const num = static_cast<unsigned char>(c);
        result += lookup_hex[(num >> 4U) & 0xfU];
        result += lookup_hex[num & 0xfU];
    }

    return result;
}
