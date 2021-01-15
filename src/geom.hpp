#ifndef OSM2PGSQL_GEOM_HPP
#define OSM2PGSQL_GEOM_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2020 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

/**
 * \file
 *
 * Low level geometry functions and types.
 */

#include <osmium/geom/coordinates.hpp>

namespace geom {

double distance(osmium::geom::Coordinates p1,
                osmium::geom::Coordinates p2) noexcept;

osmium::geom::Coordinates interpolate(osmium::geom::Coordinates p1,
                                      osmium::geom::Coordinates p2,
                                      double frac) noexcept;

} // namespace geom

#endif // OSM2PGSQL_GEOM_HPP
