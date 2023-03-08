#ifndef OSM2PGSQL_RASTER_HPP
#define OSM2PGSQL_RASTER_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <cstdint>
#include <string>

class canvas_t;
class pg_conn_t;
class tile_t;

/**
 * \file
 *
 * Helper functions for creating raster images in PostgreSQL/PostGIS.
 * https://trac.osgeo.org/postgis/wiki/WKTRaster/RFC/RFC2_V0WKBFormat
 */

struct wkb_raster_header
{
    uint8_t endianness =
#if __BYTE_ORDER == __LITTLE_ENDIAN
        1 // Little Endian
#else
        0 // Big Endian
#endif
        ;
    uint16_t version = 0;
    uint16_t nBands = 0;
    double scaleX = 0.0;
    double scaleY = 0.0;
    double ipX = 0.0;
    double ipY = 0.0;
    double skewX = 0.0;
    double skewY = 0.0;
    int32_t srid = 3857;
    uint16_t width = 0;
    uint16_t height = 0;
};

struct wkb_raster_band
{
    uint8_t bits = 0;
    uint8_t nodata = 0;
};

void add_raster_header(std::string *wkb, wkb_raster_header const &data);

void add_raster_band(std::string *wkb, wkb_raster_band const &data);

void save_image_to_file(canvas_t const &canvas, tile_t const &tile,
                        std::string const &path, std::string const &param,
                        char const *variant, unsigned int image_extent,
                        double margin);

#endif // OSM2PGSQL_RASTER_HPP
