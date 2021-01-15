/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2020 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "geom.hpp"

namespace geom {

double distance(osmium::geom::Coordinates p1,
                osmium::geom::Coordinates p2) noexcept
{
    double const dx = p1.x - p2.x;
    double const dy = p1.y - p2.y;
    return std::sqrt(dx * dx + dy * dy);
}

osmium::geom::Coordinates interpolate(osmium::geom::Coordinates p1,
                                      osmium::geom::Coordinates p2,
                                      double frac) noexcept
{
    return osmium::geom::Coordinates{frac * (p1.x - p2.x) + p2.x,
                                     frac * (p1.y - p2.y) + p2.y};
}

} // namespace geom

