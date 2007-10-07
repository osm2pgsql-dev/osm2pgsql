/*
#-----------------------------------------------------------------------------
# Part of osm2pgsql utility
#-----------------------------------------------------------------------------
# By Artem Pavlenko, Copyright 2007
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
#-----------------------------------------------------------------------------
*/

#include <iostream>

#include <geos_c.h>

#if (GEOS_VERSION_MAJOR==3)
/* geos trunk (3.0.0rc) */
#include <geos/geom/GeometryFactory.h>
#include <geos/geom/CoordinateSequenceFactory.h>
#include <geos/geom/Geometry.h>
#include <geos/geom/LineString.h>
#include <geos/geom/LinearRing.h>
#include <geos/geom/MultiLineString.h>
#include <geos/geom/Polygon.h>
#include <geos/geom/Point.h>
#include <geos/io/WKTReader.h>
#include <geos/io/WKTWriter.h>
#include <geos/util/GEOSException.h>
#include <geos/opLinemerge.h>
using namespace geos::geom;
using namespace geos::io;
using namespace geos::util;
using namespace geos::operation::linemerge;
#else
/* geos-2.2.3 */
#include <geos/geom.h>
#include <geos/io.h>
#include <geos/opLinemerge.h>
using namespace geos;
#endif

#include "build_geometry.h"

using namespace std;

char *get_wkt(osmNode *nodes, int count, int polygon, double *area) {
  GeometryFactory gf;

  auto_ptr<CoordinateSequence> coords(
    gf.getCoordinateSequenceFactory()->create(0, 2));
  for (int i = 0; i < count; i++) {
    Coordinate c;
    c.x = nodes[i].lon;
    c.y = nodes[i].lat;
    coords->add(c, i);
  }

  auto_ptr<Geometry> geom;
  if (polygon) {
    if (coords->getSize() < 4 ||
	!coords->getAt(coords->getSize() - 1).equals2D(coords->getAt(0))) {
      return 0;
    }
    auto_ptr<LinearRing> shell(gf.createLinearRing(coords.release()));
    geom = auto_ptr<Geometry>(gf.createPolygon(shell.release(),
      new vector<Geometry *>));
    *area = geom->getArea();
  } else {
    if (coords->getSize() < 2) {
      return 0;
    }
    geom = auto_ptr<Geometry>(gf.createLineString(coords.release()));
    *area = 0;
  }

  WKTWriter wktw;
  string wkt = wktw.write(geom.get());
  return strdup(wkt.c_str());
}
