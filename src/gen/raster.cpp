/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "raster.hpp"

#include "canvas.hpp"
#include "format.hpp"
#include "pgsql.hpp"
#include "tile.hpp"

#include <string>

template <typename T>
void append(std::string *str, T value)
{
    str->append(reinterpret_cast<char *>(&value), sizeof(T));
}

void add_raster_header(std::string *wkb, wkb_raster_header const &data)
{
    append(wkb, data.endianness);
    append(wkb, data.version);
    append(wkb, data.nBands);
    append(wkb, data.scaleX);
    append(wkb, data.scaleY);
    append(wkb, data.ipX);
    append(wkb, data.ipY);
    append(wkb, data.skewX);
    append(wkb, data.skewY);
    append(wkb, data.srid);
    append(wkb, data.width);
    append(wkb, data.height);
}

void add_raster_band(std::string *wkb, wkb_raster_band const &data)
{
    append(wkb, data.bits);
    append(wkb, data.nodata);
}

void save_image_to_file(canvas_t const &canvas, tile_t const &tile,
                        std::string const &path, std::string const &param,
                        char const *variant, unsigned int image_extent,
                        double margin)
{
    std::string name{fmt::format("{}-{}-{}-{}{}{}.", path, tile.x(), tile.y(),
                                 param, param.empty() ? "" : "-", variant)};

    // write image file
    canvas.save(name + "png");

    // write world file
    auto const pixel_size = tile.extent() / image_extent;
    name += "wld";
    auto *file = std::fopen(name.c_str(), "w");
    fmt::print(file, "{0}\n0.0\n0.0\n-{0}\n{1}\n{2}\n", pixel_size,
               tile.xmin(margin) + pixel_size / 2,
               tile.ymax(margin) - pixel_size / 2);
    (void)std::fclose(file);
}
