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
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <memory>
#include <new>

#if defined(__CYGWIN__)
#define GEOS_INLINE
#endif

#include <geos/geom/prep/PreparedGeometry.h>
#include <geos/geom/prep/PreparedGeometryFactory.h>
#include <geos/geom/GeometryFactory.h>
#include <geos/geom/Coordinate.h>
#include <geos/geom/CoordinateSequence.h>
#include <geos/geom/CoordinateSequenceFactory.h>
#include <geos/geom/Geometry.h>
#include <geos/geom/GeometryCollection.h>
#include <geos/geom/LineString.h>
#include <geos/geom/LinearRing.h>
#include <geos/geom/MultiLineString.h>
#include <geos/geom/Polygon.h>
#include <geos/geom/MultiPolygon.h>
#include <geos/io/WKTReader.h>
#include <geos/io/WKTWriter.h>
#include <geos/util/GEOSException.h>
#include <geos/opLinemerge.h>
using namespace geos::geom;
using namespace geos::io;
using namespace geos::util;
using namespace geos::operation::linemerge;

#include "geometry-builder.hpp"

typedef std::unique_ptr<Geometry> geom_ptr;

namespace {

void coords2nodes(CoordinateSequence * coords, nodelist_t &nodes)
{
    size_t num_coords = coords->getSize();
    nodes.reserve(num_coords);

    for (size_t i = 0; i < num_coords; i++) {
        Coordinate coord = coords->getAt(i);
        nodes.push_back(osmNode(coord.x, coord.y));
    }
}

struct polygondata
{
    Polygon*        polygon;
    LinearRing*     ring;
    double          area;
    int             iscontained;
    unsigned        containedbyid;
};

struct polygondata_comparearea {
    bool operator()(const polygondata& lhs, const polygondata& rhs) {
        return lhs.area > rhs.area;
    }
};

} // anonymous namespace

geometry_builder::maybe_wkt_t geometry_builder::get_wkt_simple(const nodelist_t &nodes, int polygon) const
{
    GeometryFactory gf;
    std::unique_ptr<CoordinateSequence> coords(gf.getCoordinateSequenceFactory()->create((size_t)0, (size_t)2));

    try
    {
        for (const auto& nd: nodes) {
            coords->add(Coordinate(nd.lon, nd.lat), 0);
        }

        maybe_wkt_t wkt(new geometry_builder::wkt_t());
        geom_ptr geom;
        if (polygon && (coords->getSize() >= 4) && (coords->getAt(coords->getSize() - 1).equals2D(coords->getAt(0)))) {
            std::unique_ptr<LinearRing> shell(gf.createLinearRing(coords.release()));
            geom = geom_ptr(gf.createPolygon(shell.release(), new std::vector<Geometry *>));
            if (!geom->isValid()) {
                if (excludepoly) {
                    throw std::runtime_error("Excluding broken polygon.");
                } else {
                    geom = geom_ptr(geom->buffer(0));
                }
            }
            geom->normalize(); // Fix direction of ring
            wkt->area = geom->getArea();
        } else {
            if (coords->getSize() < 2)
                throw std::runtime_error("Excluding degenerate line.");
            geom = geom_ptr(gf.createLineString(coords.release()));
            wkt->area = 0;
        }

        wkt->geom = WKTWriter().write(geom.get());
        return wkt;
    }
    catch (const std::bad_alloc&)
    {
        std::cerr << std::endl << "Exception caught processing way. You are likelly running out of memory." << std::endl;
        std::cerr << "Try in slim mode, using -s parameter." << std::endl;
    }
    catch (const std::runtime_error& e)
    {
        //std::cerr << std::endl << "Exception caught processing way: " << e.what() << std::endl;
    }
    catch (...)
    {
        std::cerr << std::endl << "Exception caught processing way" << std::endl;
    }

    return maybe_wkt_t();
}

geometry_builder::maybe_wkts_t geometry_builder::get_wkt_split(const nodelist_t &nodes, int polygon, double split_at) const
{
    GeometryFactory gf;
    std::unique_ptr<CoordinateSequence> coords(gf.getCoordinateSequenceFactory()->create((size_t)0, (size_t)2));
    WKTWriter writer;
    //TODO: use count to get some kind of hint of how much we should reserve?
    maybe_wkts_t wkts(new std::vector<geometry_builder::wkt_t>);

    try
    {
        for (const auto& nd: nodes) {
            coords->add(Coordinate(nd.lon, nd.lat), 0);
        }

        if (polygon && (coords->getSize() >= 4) && (coords->getAt(coords->getSize() - 1).equals2D(coords->getAt(0)))) {
            std::unique_ptr<LinearRing> shell(gf.createLinearRing(coords.release()));
            geom_ptr geom(gf.createPolygon(shell.release(), new std::vector<Geometry *>));
            if (!geom->isValid()) {
                if (excludepoly) {
                    throw std::runtime_error("Excluding broken polygon.");
                } else {
                    geom = geom_ptr(geom->buffer(0));
                }
            }
            geom->normalize(); // Fix direction of ring

            //copy of an empty one should be cheapest
            wkts->push_back(geometry_builder::wkt_t());
            //then we set on the one we already have
            wkts->back().geom = writer.write(geom.get());
            wkts->back().area = geom->getArea();

        } else {
            if (coords->getSize() < 2)
                throw std::runtime_error("Excluding degenerate line.");

            double distance = 0;
            std::unique_ptr<CoordinateSequence> segment(gf.getCoordinateSequenceFactory()->create((size_t)0, (size_t)2));
            segment->add(coords->getAt(0));
            for(unsigned i=1; i<coords->getSize(); i++) {
                const Coordinate this_pt = coords->getAt(i);
                const Coordinate prev_pt = coords->getAt(i-1);
                const double delta = this_pt.distance(prev_pt);
                assert(!std::isnan(delta));
                // figure out if the addition of this point would take the total
                // length of the line in `segment` over the `split_at` distance.

                if (distance + delta > split_at) {
                    const size_t splits = (size_t) std::floor((distance + delta) / split_at);
                    // use the splitting distance to split the current segment up
                    // into as many parts as necessary to keep each part below
                    // the `split_at` distance.
                    for (size_t i = 0; i < splits; ++i) {
                        double frac = (double(i + 1) * split_at - distance) / delta;
                        const Coordinate interpolated(frac * (this_pt.x - prev_pt.x) + prev_pt.x,
                                                      frac * (this_pt.y - prev_pt.y) + prev_pt.y);
                        segment->add(interpolated);
                        geom_ptr geom(gf.createLineString(segment.release()));

                        //copy of an empty one should be cheapest
                        wkts->push_back(geometry_builder::wkt_t());
                        //then we set on the one we already have
                        wkts->back().geom = writer.write(geom.get());
                        wkts->back().area = 0;

                        segment.reset(gf.getCoordinateSequenceFactory()->create((size_t)0, (size_t)2));
                        segment->add(interpolated);
                  }
                  // reset the distance based on the final splitting point for
                  // the next iteration.
                  distance = segment->getAt(0).distance(this_pt);

                } else {
                  // if not split then just push this point onto the sequence
                  // being saved up.
                  distance += delta;
                }

                // always add this point
                segment->add(this_pt);

                // on the last iteration, close out the line.
                if (i == coords->getSize()-1) {
                    geom_ptr geom(gf.createLineString(segment.release()));

                    //copy of an empty one should be cheapest
                    wkts->push_back(geometry_builder::wkt_t());
                    //then we set on the one we already have
                    wkts->back().geom = writer.write(geom.get());
                    wkts->back().area = 0;

                    segment.reset(gf.getCoordinateSequenceFactory()->create((size_t)0, (size_t)2));
                }
            }
        }
    }
    catch (const std::bad_alloc&)
    {
        std::cerr << std::endl << "Exception caught processing way. You are likely running out of memory." << std::endl;
        std::cerr << "Try in slim mode, using -s parameter." << std::endl;
    }
    catch (const std::runtime_error& e)
    {
        //std::cerr << std::endl << "Exception caught processing way: " << e.what() << std::endl;
    }
    catch (...)
    {
        std::cerr << std::endl << "Exception caught processing way" << std::endl;
    }
    return wkts;
}

int geometry_builder::parse_wkt(const char * wkt, multinodelist_t &nodes, int *polygon) {
    GeometryFactory gf;
    WKTReader reader(&gf);
    std::string wkt_string(wkt);
    GeometryCollection * gc;
    CoordinateSequence * coords;
    size_t num_geometries;

    *polygon = 0;
    try {
        Geometry * geometry = reader.read(wkt_string);
        switch (geometry->getGeometryTypeId()) {
            // Single geometries
            case GEOS_POLYGON:
                // Drop through
            case GEOS_LINEARRING:
                *polygon = 1;
                // Drop through
            case GEOS_POINT:
                // Drop through
            case GEOS_LINESTRING:
                nodes.push_back(nodelist_t());
                coords = geometry->getCoordinates();
                coords2nodes(coords, nodes.back());
                delete coords;
                break;
            // Geometry collections
            case GEOS_MULTIPOLYGON:
                *polygon = 1;
                // Drop through
            case GEOS_MULTIPOINT:
                // Drop through
            case GEOS_MULTILINESTRING:
                gc = dynamic_cast<GeometryCollection *>(geometry);
                num_geometries = gc->getNumGeometries();
                nodes.assign(num_geometries, nodelist_t());
                for (size_t i = 0; i < num_geometries; i++) {
                    const Geometry *subgeometry = gc->getGeometryN(i);
                    coords = subgeometry->getCoordinates();
                    coords2nodes(coords, nodes[i]);
                    delete coords;
                }
                break;
            default:
                std::cerr << std::endl << "unexpected object type while processing PostGIS data" << std::endl;
                delete geometry;
                return -1;
        }
        delete geometry;
    } catch (...) {
        std::cerr << std::endl << "Exception caught parsing PostGIS data" << std::endl;
        return -1;
    }
    return 0;
}

geometry_builder::maybe_wkts_t geometry_builder::build_polygons(const multinodelist_t &xnodes,
                                                                bool enable_multi, osmid_t osm_id) const
{
    std::unique_ptr<std::vector<Geometry*> > lines(new std::vector<Geometry*>);
    GeometryFactory gf;
    geom_ptr geom;
    geos::geom::prep::PreparedGeometryFactory pgf;

    maybe_wkts_t wkts(new std::vector<geometry_builder::wkt_t>);

    try
    {
        for (const auto& nodes: xnodes) {
            std::unique_ptr<CoordinateSequence> coords(gf.getCoordinateSequenceFactory()->create((size_t)0, (size_t)2));
            for (const auto& node: nodes) {
                Coordinate c;
                c.x = node.lon;
                c.y = node.lat;
                coords->add(c, 0);
            }
            if (coords->getSize() > 1) {
                geom = geom_ptr(gf.createLineString(coords.release()));
                lines->push_back(geom.release());
            }
        }

        //geom_ptr segment(0);
        geom_ptr mline (gf.createMultiLineString(lines.release()));
        //geom_ptr noded (segment->Union(mline.get()));
        LineMerger merger;
        //merger.add(noded.get());
        merger.add(mline.get());
        std::unique_ptr<std::vector<LineString *>> merged(merger.getMergedLineStrings());
        WKTWriter writer;

        // Procces ways into lines or simple polygon list
        std::vector<polygondata> polys(merged->size());

        unsigned totalpolys = 0;
        for (unsigned i=0 ;i < merged->size(); ++i)
        {
            std::unique_ptr<LineString> pline ((*merged ) [i]);
            if (pline->getNumPoints() > 3 && pline->isClosed())
            {
                polys[totalpolys].polygon = gf.createPolygon(gf.createLinearRing(pline->getCoordinates()),0);
                polys[totalpolys].ring = gf.createLinearRing(pline->getCoordinates());
                polys[totalpolys].area = polys[totalpolys].polygon->getArea();
                polys[totalpolys].iscontained = 0;
                polys[totalpolys].containedbyid = 0;
                if (polys[totalpolys].area > 0.0)
                    totalpolys++;
                else {
                    delete(polys[totalpolys].polygon);
                    delete(polys[totalpolys].ring);
                }
            }
        }

        if (totalpolys)
        {
            std::sort(polys.begin(), polys.begin() + totalpolys, polygondata_comparearea());

            unsigned toplevelpolygons = 0;
            int istoplevelafterall;

            for (unsigned i=0 ;i < totalpolys; ++i)
            {
                if (polys[i].iscontained != 0) continue;
                toplevelpolygons++;
                const geos::geom::prep::PreparedGeometry* preparedtoplevelpolygon = pgf.create(polys[i].polygon);

                for (unsigned j=i+1; j < totalpolys; ++j)
                {
                    // Does preparedtoplevelpolygon contain the smaller polygon[j]?
                    if (polys[j].containedbyid == 0 && preparedtoplevelpolygon->contains(polys[j].polygon))
                    {
                        // are we in a [i] contains [k] contains [j] situation
                        // which would actually make j top level
                        istoplevelafterall = 0;
                        for (unsigned k=i+1; k < j; ++k)
                        {
                            if (polys[k].iscontained && polys[k].containedbyid == i && polys[k].polygon->contains(polys[j].polygon))
                            {
                                istoplevelafterall = 1;
                                break;
                            }
                        }
                        if (istoplevelafterall == 0)
                        {
                            polys[j].iscontained = 1;
                            polys[j].containedbyid = i;
                        }
                    }
                }
                pgf.destroy(preparedtoplevelpolygon);
            }
            // polys now is a list of polygons tagged with which ones are inside each other

            // List of polygons for multipolygon
            std::unique_ptr<std::vector<Geometry*>> polygons(new std::vector<Geometry*>);

            // For each top level polygon create a new polygon including any holes
            for (unsigned i=0 ;i < totalpolys; ++i)
            {
                if (polys[i].iscontained != 0) continue;

                // List of holes for this top level polygon
                std::unique_ptr<std::vector<Geometry*> > interior(new std::vector<Geometry*>);
                for (unsigned j=i+1; j < totalpolys; ++j)
                {
                   if (polys[j].iscontained == 1 && polys[j].containedbyid == i)
                   {
                       interior->push_back(polys[j].ring);
                   }
                }

                Polygon* poly(gf.createPolygon(polys[i].ring, interior.release()));
                poly->normalize();
                polygons->push_back(poly);
            }

            // Make a multipolygon if required
            if ((toplevelpolygons > 1) && enable_multi)
            {
                geom_ptr multipoly(gf.createMultiPolygon(polygons.release()));
                if (!multipoly->isValid() && !excludepoly) {
                    multipoly = geom_ptr(multipoly->buffer(0));
                }
                multipoly->normalize();

                if ((excludepoly == 0) || (multipoly->isValid()))
                {
                    //copy of an empty one should be cheapest
                    wkts->push_back(geometry_builder::wkt_t());
                    //then we set on the one we already have
                    wkts->back().geom = writer.write(multipoly.get());
                    wkts->back().area = multipoly->getArea();
                }
            }
            else
            {
                for(unsigned i=0; i<toplevelpolygons; i++)
                {
                    Geometry* poly = dynamic_cast<Geometry*>(polygons->at(i));
                    if (!poly->isValid() && !excludepoly) {
                        poly = dynamic_cast<Geometry*>(poly->buffer(0));
                        poly->normalize();
                    }
                    if ((excludepoly == 0) || (poly->isValid()))
                    {
                        //copy of an empty one should be cheapest
                        wkts->push_back(geometry_builder::wkt_t());
                        //then we set on the one we already have
                        wkts->back().geom = writer.write(poly);
                        wkts->back().area = poly->getArea();
                    }
                    delete(poly);
                }
            }
        }

        for (unsigned i=0; i < totalpolys; ++i)
        {
            delete(polys[i].polygon);
        }
    }//TODO: don't show in message id when osm_id == -1
    catch (const std::exception& e)
    {
        std::cerr << std::endl << "Standard exception processing way_id="<< osm_id << ": " << e.what()  << std::endl;
    }
    catch (...)
    {
        std::cerr << std::endl << "Exception caught processing way id=" << osm_id << std::endl;
    }

    return wkts;
}

geometry_builder::maybe_wkt_t geometry_builder::build_multilines(const multinodelist_t &xnodes, osmid_t osm_id) const
{
    std::unique_ptr<std::vector<Geometry*> > lines(new std::vector<Geometry*>);
    GeometryFactory gf;
    geom_ptr geom;

    maybe_wkt_t wkt(new geometry_builder::wkt_t());

    try
    {
        for (const auto& nodes: xnodes) {
            std::unique_ptr<CoordinateSequence> coords(gf.getCoordinateSequenceFactory()->create((size_t)0, (size_t)2));
            for (const auto& node: nodes) {
                Coordinate c;
                c.x = node.lon;
                c.y = node.lat;
                coords->add(c, 0);
            }
            if (coords->getSize() > 1) {
                geom = geom_ptr(gf.createLineString(coords.release()));
                lines->push_back(geom.release());
            }
        }

        //geom_ptr segment(0);
        geom_ptr mline (gf.createMultiLineString(lines.release()));
        //geom_ptr noded (segment->Union(mline.get()));

        WKTWriter writer;
        wkt->geom = writer.write(mline.get());
        wkt->area = 0;
    }//TODO: don't show in message id when osm_id == -1
    catch (const std::exception& e)
    {
        std::cerr << std::endl << "Standard exception processing way_id="<< osm_id << ": " << e.what()  << std::endl;
    }
    catch (...)
    {
        std::cerr << std::endl << "Exception caught processing way id=" << osm_id << std::endl;
    }
    return wkt;
}

geometry_builder::maybe_wkts_t geometry_builder::build_both(const multinodelist_t &xnodes,
                                                            int make_polygon, int enable_multi,
                                                            double split_at, osmid_t osm_id) const
{
    std::unique_ptr<std::vector<Geometry*> > lines(new std::vector<Geometry*>);
    GeometryFactory gf;
    geom_ptr geom;
    geos::geom::prep::PreparedGeometryFactory pgf;
    maybe_wkts_t wkts(new std::vector<geometry_builder::wkt_t>);


    try
    {
      for (const auto& nodes: xnodes) {
            std::unique_ptr<CoordinateSequence> coords(gf.getCoordinateSequenceFactory()->create((size_t)0, (size_t)2));
            for (const auto& node: nodes) {
                Coordinate c;
                c.x = node.lon;
                c.y = node.lat;
                coords->add(c, 0);
            }
            if (coords->getSize() > 1) {
                geom = geom_ptr(gf.createLineString(coords.release()));
                lines->push_back(geom.release());
            }
        }

        //geom_ptr segment(0);
        geom_ptr mline (gf.createMultiLineString(lines.release()));
        //geom_ptr noded (segment->Union(mline.get()));
        LineMerger merger;
        //merger.add(noded.get());
        merger.add(mline.get());
        std::unique_ptr<std::vector<LineString *> > merged(merger.getMergedLineStrings());
        WKTWriter writer;

        // Procces ways into lines or simple polygon list
        std::vector<polygondata> polys(merged->size());

        unsigned totalpolys = 0;
        for (unsigned i=0 ;i < merged->size(); ++i)
        {
            std::unique_ptr<LineString> pline ((*merged ) [i]);
            if (make_polygon && pline->getNumPoints() > 3 && pline->isClosed())
            {
                polys[totalpolys].polygon = gf.createPolygon(gf.createLinearRing(pline->getCoordinates()),0);
                polys[totalpolys].ring = gf.createLinearRing(pline->getCoordinates());
                polys[totalpolys].area = polys[totalpolys].polygon->getArea();
                polys[totalpolys].iscontained = 0;
                polys[totalpolys].containedbyid = 0;
                if (polys[totalpolys].area > 0.0)
                    totalpolys++;
                else {
                    delete(polys[totalpolys].polygon);
                    delete(polys[totalpolys].ring);
                }
            }
            else
            {
                //std::cerr << "polygon(" << osm_id << ") is no good: points(" << pline->getNumPoints() << "), closed(" << pline->isClosed() << "). " << writer.write(pline.get()) << std::endl;
                double distance = 0;
                std::unique_ptr<CoordinateSequence> segment;
                segment = std::unique_ptr<CoordinateSequence>(gf.getCoordinateSequenceFactory()->create((size_t)0, (size_t)2));
                segment->add(pline->getCoordinateN(0));
                for(unsigned i=1; i<pline->getNumPoints(); i++) {
                    segment->add(pline->getCoordinateN(i));
                    distance += pline->getCoordinateN(i).distance(pline->getCoordinateN(i-1));
                    if ((distance >= split_at) || (i == pline->getNumPoints()-1)) {
                        geom_ptr geom = geom_ptr(gf.createLineString(segment.release()));

                        //copy of an empty one should be cheapest
                        wkts->push_back(geometry_builder::wkt_t());
                        //then we set on the one we already have
                        wkts->back().geom = writer.write(geom.get());
                        wkts->back().area = 0;

                        segment.reset(gf.getCoordinateSequenceFactory()->create((size_t)0, (size_t)2));
                        distance=0;
                        segment->add(pline->getCoordinateN(i));
                    }
                }
                //std::string text = writer.write(pline.get());
                //wkts.push_back(text);
                //areas.push_back(0.0);
                //wkt_size++;
            }
        }

        if (totalpolys)
        {
            std::sort(polys.begin(), polys.begin() + totalpolys, polygondata_comparearea());

            unsigned toplevelpolygons = 0;
            int istoplevelafterall;

            for (unsigned i=0 ;i < totalpolys; ++i)
            {
                if (polys[i].iscontained != 0) continue;
                toplevelpolygons++;
                const geos::geom::prep::PreparedGeometry* preparedtoplevelpolygon = pgf.create(polys[i].polygon);

                for (unsigned j=i+1; j < totalpolys; ++j)
                {
                    // Does preparedtoplevelpolygon contain the smaller polygon[j]?
                    if (polys[j].containedbyid == 0 && preparedtoplevelpolygon->contains(polys[j].polygon))
                    {
                        // are we in a [i] contains [k] contains [j] situation
                        // which would actually make j top level
                        istoplevelafterall = 0;
                        for (unsigned k=i+1; k < j; ++k)
                        {
                            if (polys[k].iscontained && polys[k].containedbyid == i && polys[k].polygon->contains(polys[j].polygon))
                            {
                                istoplevelafterall = 1;
                                break;
                            }
#if 0
                            else if (polys[k].polygon->intersects(polys[j].polygon) || polys[k].polygon->touches(polys[j].polygon))
                            {
                                // FIXME: This code does not work as intended
                                // It should be setting the polys[k].ring in order to update this object
                                // but the value of polys[k].polygon calculated is normally NULL

                                // Add polygon this polygon (j) to k since they intersect
                                // Mark ourselfs to be dropped (2), delete the original k
                                Geometry* polyunion = polys[k].polygon->Union(polys[j].polygon);
                                delete(polys[k].polygon);
                                polys[k].polygon = dynamic_cast<Polygon*>(polyunion);
                                polys[j].iscontained = 2; // Drop
                                istoplevelafterall = 2;
                                break;
                            }
#endif
                        }
                        if (istoplevelafterall == 0)
                        {
                            polys[j].iscontained = 1;
                            polys[j].containedbyid = i;
                        }
                    }
                }
              pgf.destroy(preparedtoplevelpolygon);
            }
            // polys now is a list of polygons tagged with which ones are inside each other

            // List of polygons for multipolygon
            std::unique_ptr<std::vector<Geometry*> > polygons(new std::vector<Geometry*>);

            // For each top level polygon create a new polygon including any holes
            for (unsigned i=0 ;i < totalpolys; ++i)
            {
                if (polys[i].iscontained != 0) continue;

                // List of holes for this top level polygon
                std::unique_ptr<std::vector<Geometry*> > interior(new std::vector<Geometry*>);
                for (unsigned j=i+1; j < totalpolys; ++j)
                {
                   if (polys[j].iscontained == 1 && polys[j].containedbyid == i)
                   {
                       interior->push_back(polys[j].ring);
                   }
                }

                Polygon* poly(gf.createPolygon(polys[i].ring, interior.release()));
                poly->normalize();
                polygons->push_back(poly);
            }

            // Make a multipolygon if required
            if ((toplevelpolygons > 1) && enable_multi)
            {
                geom_ptr multipoly(gf.createMultiPolygon(polygons.release()));
                if (!multipoly->isValid() && !excludepoly) {
                    multipoly = geom_ptr(multipoly->buffer(0));
                }
                multipoly->normalize();

                if ((excludepoly == 0) || (multipoly->isValid()))
                {
                    //copy of an empty one should be cheapest
                    wkts->push_back(geometry_builder::wkt_t());
                    //then we set on the one we already have
                    wkts->back().geom = writer.write(multipoly.get());
                    wkts->back().area = multipoly->getArea();
                }
            }
            else
            {
                for(unsigned i=0; i<toplevelpolygons; i++)
                {
                    Geometry* poly = dynamic_cast<Geometry*>(polygons->at(i));
                    if (!poly->isValid() && !excludepoly) {
                        poly = dynamic_cast<Geometry*>(poly->buffer(0));
                        poly->normalize();
                    }
                    if (!excludepoly || (poly->isValid()))
                    {
                        //copy of an empty one should be cheapest
                        wkts->push_back(geometry_builder::wkt_t());
                        //then we set on the one we already have
                        wkts->back().geom = writer.write(poly);
                        wkts->back().area = poly->getArea();
                    }
                    delete(poly);
                }
            }
        }

        for (unsigned i=0; i < totalpolys; ++i)
        {
            delete(polys[i].polygon);
        }
    }//TODO: don't show in message id when osm_id == -1
    catch (const std::exception& e)
    {
        std::cerr << std::endl << "Standard exception processing relation id="<< osm_id << ": " << e.what()  << std::endl;
    }
    catch (...)
    {
        std::cerr << std::endl << "Exception caught processing relation id=" << osm_id << std::endl;
    }

    return wkts;
}
