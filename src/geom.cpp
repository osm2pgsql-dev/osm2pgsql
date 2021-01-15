/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2020 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "geom.hpp"

#include <algorithm>
#include <iterator>

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

linestring_t::linestring_t(
    std::initializer_list<osmium::geom::Coordinates> coords)
{
    std::copy(coords.begin(), coords.end(), std::back_inserter(m_coordinates));
}

linestring_t::linestring_t(osmium::NodeRefList const &nodes,
                           reprojection const &proj)
{
    osmium::Location last{};
    for (auto const &node : nodes) {
        auto const loc = node.location();
        if (loc.valid() && loc != last) {
            add_point(proj.reproject(loc));
            last = loc;
        }
    }

    if (size() <= 1) {
        m_coordinates.clear();
    }
}

} // namespace geom

