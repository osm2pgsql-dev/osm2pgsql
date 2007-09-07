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
#include <geos/opLinemerge.h>
using namespace geos::geom;
using namespace geos::io;
using namespace geos::operation::linemerge;
#else
/* geos-2.2.3 */
#include <geos/geom.h>
#include <geos/io.h>
#include <geos/opLinemerge.h>
using namespace geos;
#endif

#include "build_geometry.h"

typedef std::auto_ptr<Geometry> geom_ptr;

struct Segment
{
      Segment(double x0_,double y0_,double x1_,double y1_)
         :x0(x0_),y0(y0_),x1(x1_),y1(y1_) {}

      double x0;
      double y0;
      double x1;
      double y1;
};

struct Interior
{
	Interior(double x_, double y_)
	  : x(x_), y(y_) {}

	Interior(Geometry *geom) {
            try {
                std::auto_ptr<Point> pt(geom->getInteriorPoint());
                x = pt->getX();
                y = pt->getY();
            } catch (...) {
                // This happens on some unusual polygons, we'll ignore them for now
                //std::cerr << std::endl << "Exception finding interior point" << std::endl;
                x=y=0.0;
            }
        }
	double x, y;
};

static std::vector<Segment> segs;
static std::vector<std::string> wkts;
static std::vector<Interior> interiors;
static std::vector<double> areas;


int is_simple(const char* wkt)
{
   GeometryFactory factory;
   WKTReader reader(&factory);
   geom_ptr geom(reader.read(wkt));
   if (geom->isSimple()) return 1;
   return 0;
}

void add_segment(double x0,double y0,double x1,double y1)
{
   segs.push_back(Segment(x0,y0,x1,y1));
}

char * get_wkt(size_t index)
{
//   return wkts[index].c_str();
	char *result;
	result = (char*) std::malloc( wkts[index].length() + 1);
	std::strcpy(result, wkts[index].c_str() );
	return result;
}

void get_interior(size_t index, double *y, double *x)
{
	*x = interiors[index].x;
	*y = interiors[index].y;
}

double get_area(size_t index)
{
    return areas[index];
}

void clear_wkts()
{
   wkts.clear();
   interiors.clear();
   areas.clear();
}

size_t build_geometry(int polygon, int osm_id)
{
    size_t wkt_size = 0;
    GeometryFactory factory;
    geom_ptr segment(0);
    std::auto_ptr<std::vector<Geometry*> > lines(new std::vector<Geometry*>);
    std::vector<Segment>::const_iterator pos=segs.begin();
    std::vector<Segment>::const_iterator end=segs.end();
    bool first=true;
    try  {
        while (pos != end)
        {
            if (pos->x0 != pos->x1 || pos->y0 != pos->y1)
            {
                std::auto_ptr<CoordinateSequence> coords(factory.getCoordinateSequenceFactory()->create(0,2));
                coords->add(Coordinate(pos->x0,pos->y0));
                coords->add(Coordinate(pos->x1,pos->y1));
                geom_ptr linestring(factory.createLineString(coords.release()));
                if (first)
                {
                    segment = linestring;
                    first=false;
                }
                else
                {
                    lines->push_back(linestring.release());
                }
            }
            ++pos;
        }

        segs.clear();

        if (segment.get())
        {
            geom_ptr mline (factory.createMultiLineString(lines.release()));
            geom_ptr noded (segment->Union(mline.get()));
            LineMerger merger;
            merger.add(noded.get());
            std::auto_ptr<std::vector<LineString *> > merged(merger.getMergedLineStrings());
            WKTWriter writer;

            if (polygon)
            {
                std::auto_ptr<LinearRing> exterior;
                std::auto_ptr<std::vector<Geometry*> > interior(new std::vector<Geometry*>);
                double ext_area = 0.0;
                for (unsigned i=0 ;i < merged->size(); ++i)
                {
                    std::auto_ptr<LineString> pline ((*merged ) [i]);
                    if (pline->getNumPoints() > 3 && pline->isClosed())
                    {
                        std::auto_ptr<LinearRing> ring(factory.createLinearRing(pline->getCoordinates()));
                        std::auto_ptr<Polygon> poly(factory.createPolygon(factory.createLinearRing(pline->getCoordinates()),0));
                        double poly_area = poly->getArea();
                        if (poly_area > ext_area) {
                            if (ext_area > 0.0)
                                interior->push_back(exterior.release());
                            ext_area = poly_area;
                            exterior = ring;
                        //std::cerr << "Found bigger ring, area(" << ext_area << ") " << writer.write(poly.get()) << std::endl;
                        } else {
                            interior->push_back(ring.release());
                        //std::cerr << "Found inner ring, area(" << poly->getArea() << ") " << writer.write(poly.get()) << std::endl;
                        }
                    } else {
                        //std::cerr << "polygon(" << osm_id << ") is no good: points(" << pline->getNumPoints() << "), closed(" << pline->isClosed() << "). " << writer.write(pline.get()) << std::endl;
                        std::string text = writer.write(pline.get());
                        wkts.push_back(text);
                        interiors.push_back(Interior(0,0));
                        areas.push_back(0.0);
                        wkt_size++;
                    }
                }

                if (ext_area > 0.0) {
                    std::auto_ptr<Polygon> poly(factory.createPolygon(exterior.release(), interior.release()));
                    std::string text = writer.write(poly.get());
                    //std::cerr << "Result: area(" << poly->getArea() << ") " << writer.write(poly.get()) << std::endl;
                    wkts.push_back(text);
                    interiors.push_back(Interior(poly.get()));
                    areas.push_back(ext_area);
                    wkt_size++;
                }
            } else {
                for (unsigned i=0 ;i < merged->size(); ++i)
                {
                    std::auto_ptr<LineString> pline ((*merged ) [i]);
                    std::string text = writer.write(pline.get());
                    wkts.push_back(text);
                    interiors.push_back(Interior(0,0));
                    areas.push_back(0.0);
                    ++wkt_size;
                }
            }
        }
    }
    catch (...)
    {
        std::cerr << std::endl << "excepton caught processing way id=" << osm_id << std::endl;
        wkt_size = 0;
    }
    return wkt_size;
}

