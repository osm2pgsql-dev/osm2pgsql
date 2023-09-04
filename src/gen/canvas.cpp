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

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

void canvas_t::open_close(unsigned int buffer_size)
{
    auto const kernel1 = cv::getStructuringElement(
        cv::MORPH_RECT,
        cv::Size(static_cast<int>(buffer_size), static_cast<int>(buffer_size)));
    auto const kernel2 = cv::getStructuringElement(
        cv::MORPH_RECT, cv::Size(static_cast<int>(buffer_size * 2),
                                 static_cast<int>(buffer_size * 2)));

    cv::erode(m_rast, m_rast, kernel1);
    cv::dilate(m_rast, m_rast, kernel2);
    cv::erode(m_rast, m_rast, kernel1);
}

void canvas_t::create_pointlist(std::vector<cv::Point> *out,
                                geom::point_list_t const &pl,
                                tile_t const &tile) const
{
    out->reserve(pl.size());

    for (auto const point : pl) {
        auto const tp = tile.to_tile_coords(point, m_extent);
        auto const x = static_cast<double>(m_buffer) + tp.x();
        auto const y = static_cast<double>(m_buffer + m_extent) - tp.y();
        out->emplace_back(x, y);
    }
}

std::size_t canvas_t::draw_polygon(geom::polygon_t const &polygon,
                                   tile_t const &tile)
{
    std::size_t num_points = polygon.outer().size();

    std::vector<std::vector<cv::Point>> poly_data;
    poly_data.resize(polygon.inners().size() + 1);

    create_pointlist(poly_data.data(), polygon.outer(), tile);
    std::size_t n = 1;
    for (auto const &inner : polygon.inners()) {
        num_points += inner.size();
        create_pointlist(&poly_data[n], inner, tile);
        n++;
    }
    cv::fillPoly(m_rast, poly_data, cv::Scalar{255});

    return num_points;
}

std::size_t canvas_t::draw_linestring(geom::linestring_t const &linestring,
                                      tile_t const &tile)
{
    std::vector<cv::Point> line_data;
    create_pointlist(&line_data, linestring, tile);
    cv::polylines(m_rast, line_data, false, cv::Scalar{255});
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
    cv::imwrite(filename, m_rast);
}

std::string canvas_t::to_wkb(tile_t const &tile, double margin) const
{
    std::string wkb;
    wkb.reserve(61 + 2 + size() * size());

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
    wkb.append(reinterpret_cast<char const *>(begin()),
               reinterpret_cast<char const *>(end()));

    assert(wkb.size() == 61 + 2 + size() * size());

    return wkb;
}

void canvas_t::merge(canvas_t const &other)
{
    cv::bitwise_or(m_rast, other.m_rast, m_rast);
}

std::string to_hex(std::string const &in)
{
    std::string result;
    result.reserve(in.size() * 2);

    char const *const lookup_hex = "0123456789ABCDEF";

    for (const auto c : in) {
        unsigned int const num = static_cast<unsigned char>(c);
        result += lookup_hex[(num >> 4U) & 0xfU];
        result += lookup_hex[num & 0xfU];
    }

    return result;
}
