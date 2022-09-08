#ifndef OSM2PGSQL_GEOM_POLE_OF_INACCESSIBILITY_HPP
#define OSM2PGSQL_GEOM_POLE_OF_INACCESSIBILITY_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "geom.hpp"

namespace geom {

/**
 * Finding the "pole of inaccessibility", the most distant internal point from
 * the polygon outline, or center of the maximum inscribed circle.
 *
 * \param polygon The input polygon
 * \param precision Used as cutoff in the recursive algorithm. A minimum cutoff
 *        is also set at max(width, height) of the polygon envelope / 1000.0.
 * \param stretch Stretch factor. If this is set to something other than 1.0,
 *        the maximum inscribed circle is stretched (or compressed). With
 *        values > 1.0 the algorithm will prefer places where the polygon is
 *        wider which can be useful for labels.
 *
 * \pre \code stretch > 0 \endcode
 */
point_t pole_of_inaccessibility(const polygon_t &polygon, double precision,
                                double stretch = 1.0);

void pole_of_inaccessibility(geometry_t *output, geometry_t const &input,
                             double precision, double stretch = 1.0);

geometry_t pole_of_inaccessibility(geometry_t const &input, double precision,
                                   double stretch = 1.0);

} // namespace geom

#endif // OSM2PGSQL_GEOM_POLE_OF_INACCESSIBILITY_HPP
