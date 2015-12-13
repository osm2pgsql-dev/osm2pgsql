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
#include <sstream>
#include <stdexcept>
#include <memory>
#include <new>
#include <numeric>

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
#include <geos/io/WKBReader.h>
#include <geos/io/WKBWriter.h>
#include <geos/util/GEOSException.h>
#include <geos/opLinemerge.h>
using namespace geos::geom;
using namespace geos::util;
using namespace geos::operation::linemerge;

#include "geometry-builder.hpp"
#include "reprojection.hpp"

typedef std::unique_ptr<Geometry> geom_ptr;
typedef std::unique_ptr<CoordinateSequence> coord_ptr;

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

coord_ptr nodes2coords(GeometryFactory &gf, const nodelist_t &nodes)
{
    coord_ptr coords(gf.getCoordinateSequenceFactory()->create(size_t(0), size_t(2)));

    for (const auto& nd: nodes) {
        coords->add(Coordinate(nd.lon, nd.lat), 0);
    }

    return coords;
}

geom_ptr create_multi_line(GeometryFactory &gf, const multinodelist_t &xnodes)
{
    // XXX leaks memory if an exception is thrown
    std::unique_ptr<std::vector<Geometry*> > lines(new std::vector<Geometry*>);
    lines->reserve(xnodes.size());

    for (const auto& nodes: xnodes) {
        auto coords = nodes2coords(gf, nodes);
        if (coords->getSize() > 1) {
            lines->push_back(gf.createLineString(coords.release()));
        }
    }

    return geom_ptr(gf.createMultiLineString(lines.release()));
}

bool is_polygon_line(CoordinateSequence * coords)
{
    return (coords->getSize() >= 4)
           && (coords->getAt(coords->getSize() - 1).equals2D(coords->getAt(0)));
}

/**
 * Reprojects given Linear Ring from target projection to spherical mercator.
 * Caller takes ownership of return value.
 */
LinearRing* reproject_linearring(const LineString *ls, const reprojection *proj)
{
    auto *gf = ls->getFactory();
    coord_ptr coords(gf->getCoordinateSequenceFactory()->create(size_t(0), size_t(2)));
    for (auto i : *(ls->getCoordinatesRO()->toVector())) {
        Coordinate c(i.x, i.y);
        proj->target_to_tile(&c.y, &c.x);
        coords->add(c);
    }
    return gf->createLinearRing(coords.release());
}


/**
 * Computes area of given polygonal geometry.
 * \return the area in projected units, or in EPSG 3857 if area reprojection is enabled
 */
double get_area(const geos::geom::Geometry *geom, reprojection *proj)
{
    // reprojection is not necessary, or has not been asked for.
    if (!proj) {
        return geom->getArea();
    }

    // MultiPolygon - return sum of individual areas
    if (const auto *multi = dynamic_cast<const MultiPolygon *>(geom)) {
        return std::accumulate(multi->begin(), multi->end(), 0.0,
                               [=](double a, const Geometry *g) {
                                 return a + get_area(g, proj);
                               });
    }

    const auto *poly = dynamic_cast<const geos::geom::Polygon *>(geom);
    if (!poly) {
        return 0.0;
    }

    // standard polygon - reproject rings individually, then assemble polygon and
    // compute area.

    const auto *ext = poly->getExteriorRing();
    std::unique_ptr<LinearRing> projectedExt(reproject_linearring(ext, proj));
    auto nholes = poly->getNumInteriorRing();
    std::unique_ptr<std::vector<Geometry *> > projectedHoles(new std::vector<Geometry *>);
    for (std::size_t i=0; i < nholes; i++) {
        auto* hole = poly->getInteriorRingN(i);
        projectedHoles->push_back(reproject_linearring(hole, proj));
    }
    const geom_ptr projectedPoly(poly->getFactory()->createPolygon(projectedExt.release(), projectedHoles.release()));

    return projectedPoly->getArea();
}


struct polygondata
{
    std::unique_ptr<Polygon>    polygon;
    std::unique_ptr<LinearRing> ring;
    double          area;
    bool            iscontained;
    unsigned        containedbyid;

    polygondata(std::unique_ptr<Polygon> p, LinearRing* r, double a)
    : polygon(std::move(p)), ring(r), area(a),
      iscontained(false), containedbyid(0)
    {}
};

struct polygondata_comparearea {
    bool operator()(const polygondata& lhs, const polygondata& rhs) {
        return lhs.area > rhs.area;
    }
};

} // anonymous namespace


void geometry_builder::pg_geom_t::set(const geos::geom::Geometry *g, bool poly,
                                      reprojection *p)
{
    geos::io::WKBWriter writer(2, getMachineByteOrder(), true);
    std::stringstream stream(std::ios_base::out);
    writer.writeHEX(*g, stream);
    geom = stream.str();

    if (valid()) {
        area = poly ? get_area(g, p) : 0;
        polygon = poly;
    }
}

geom_ptr geometry_builder::create_simple_poly(GeometryFactory &gf,
                                              std::unique_ptr<CoordinateSequence> coords) const
{
    std::unique_ptr<LinearRing> shell(gf.createLinearRing(coords.release()));
    std::unique_ptr<std::vector<Geometry *> > empty(new std::vector<Geometry *>);
    geom_ptr geom(gf.createPolygon(shell.release(), empty.release()));

    if (!geom->isValid()) {
        if (excludepoly) {
            throw std::runtime_error("Excluding broken polygon.");
        } else {
            geom = geom_ptr(geom->buffer(0));
        }
    }
    geom->normalize(); // Fix direction of ring

    return geom;
}

geometry_builder::pg_geom_t geometry_builder::get_wkb_simple(const nodelist_t &nodes, int polygon) const
{
    pg_geom_t wkb;

    try
    {
        GeometryFactory gf;
        auto coords = nodes2coords(gf, nodes);
        if (polygon && is_polygon_line(coords.get())) {
            auto geom = create_simple_poly(gf, std::move(coords));
            wkb.set(geom.get(), true, projection);
        } else {
            if (coords->getSize() < 2)
                throw std::runtime_error("Excluding degenerate line.");
            geom_ptr geom(gf.createLineString(coords.release()));
            wkb.set(geom.get(), false);
        }
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

    return wkb;
}

geometry_builder::pg_geoms_t geometry_builder::get_wkb_split(const nodelist_t &nodes, int polygon, double split_at) const
{
    //TODO: use count to get some kind of hint of how much we should reserve?
    pg_geoms_t wkbs;

    try
    {
        GeometryFactory gf;
        auto coords = nodes2coords(gf, nodes);

        if (polygon && is_polygon_line(coords.get())) {
            auto geom = create_simple_poly(gf, std::move(coords));
            wkbs.emplace_back(geom.get(), true, projection);
        } else {
            if (coords->getSize() < 2)
                throw std::runtime_error("Excluding degenerate line.");

            double distance = 0;
            std::unique_ptr<CoordinateSequence> segment(gf.getCoordinateSequenceFactory()->create((size_t)0, (size_t)2));
            segment->add(coords->getAt(0));
            for(size_t i=1; i<coords->getSize(); i++) {
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
                    for (size_t j = 0; j < splits; ++j) {
                        double frac = (double(j + 1) * split_at - distance) / delta;
                        const Coordinate interpolated(frac * (this_pt.x - prev_pt.x) + prev_pt.x,
                                                      frac * (this_pt.y - prev_pt.y) + prev_pt.y);
                        segment->add(interpolated);
                        geom_ptr geom(gf.createLineString(segment.release()));

                        wkbs.emplace_back(geom.get(), false);

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

                    wkbs.emplace_back(geom.get(), false);
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
    return wkbs;
}

int geometry_builder::parse_wkb(const char* wkb, multinodelist_t &nodes, bool *polygon) {
    GeometryFactory gf;
    geos::io::WKBReader reader(gf);

    *polygon = false;
    std::stringstream stream(wkb, std::ios_base::in);
    geom_ptr geometry(reader.readHEX(stream));
    switch (geometry->getGeometryTypeId()) {
        // Single geometries
        case GEOS_POLYGON:
            // Drop through
        case GEOS_LINEARRING:
            *polygon = true;
            // Drop through
        case GEOS_POINT:
            // Drop through
        case GEOS_LINESTRING:
        {
            nodes.push_back(nodelist_t());
            coord_ptr coords(geometry->getCoordinates());
            coords2nodes(coords.get(), nodes.back());
            break;
        }
        // Geometry collections
        case GEOS_MULTIPOLYGON:
            *polygon = true;
            // Drop through
        case GEOS_MULTIPOINT:
            // Drop through
        case GEOS_MULTILINESTRING:
        {
            auto gc = dynamic_cast<GeometryCollection *>(geometry.get());
            size_t num_geometries = gc->getNumGeometries();
            nodes.assign(num_geometries, nodelist_t());
            for (size_t i = 0; i < num_geometries; i++) {
                const Geometry *subgeometry = gc->getGeometryN(i);
                coord_ptr coords(subgeometry->getCoordinates());
                coords2nodes(coords.get(), nodes[i]);
            }
            break;
        }
        default:
            std::cerr << std::endl << "unexpected object type while processing PostGIS data" << std::endl;
            return -1;
    }

    return 0;
}

geometry_builder::pg_geoms_t geometry_builder::build_polygons(const multinodelist_t &xnodes,
                                                              bool enable_multi, osmid_t osm_id) const
{
    pg_geoms_t wkbs;

    try
    {
        GeometryFactory gf;
        geom_ptr mline = create_multi_line(gf, xnodes);

        //geom_ptr noded (segment->Union(mline.get()));
        LineMerger merger;
        //merger.add(noded.get());
        merger.add(mline.get());
        std::unique_ptr<std::vector<LineString *>> merged(merger.getMergedLineStrings());

        // Procces ways into lines or simple polygon list
        std::vector<polygondata> polys;
        polys.reserve(merged->size());

        for (auto *line: *merged) {
            // stuff into unique pointer for auto-destruct
            std::unique_ptr<LineString> pline(line);
            if (pline->getNumPoints() > 3 && pline->isClosed()) {
                std::unique_ptr<Polygon> poly(gf.createPolygon(gf.createLinearRing(pline->getCoordinates()),0));
                double area = get_area(poly.get(), projection);
                if (area > 0.0) {
                    polys.emplace_back(std::move(poly),
                                       gf.createLinearRing(pline->getCoordinates()),
                                       area);
                }
            }
        }

        if (!polys.empty())
        {
            std::sort(polys.begin(), polys.end(), polygondata_comparearea());

            unsigned toplevelpolygons = 0;
            int istoplevelafterall;
            size_t totalpolys = polys.size();

            geos::geom::prep::PreparedGeometryFactory pgf;
            for (unsigned i=0 ;i < totalpolys; ++i)
            {
                if (polys[i].iscontained) continue;
                toplevelpolygons++;
                const geos::geom::prep::PreparedGeometry* preparedtoplevelpolygon = pgf.create(polys[i].polygon.get());

                for (unsigned j=i+1; j < totalpolys; ++j)
                {
                    // Does preparedtoplevelpolygon contain the smaller polygon[j]?
                    if (polys[j].containedbyid == 0 && preparedtoplevelpolygon->contains(polys[j].polygon.get()))
                    {
                        // are we in a [i] contains [k] contains [j] situation
                        // which would actually make j top level
                        istoplevelafterall = 0;
                        for (unsigned k=i+1; k < j; ++k)
                        {
                            if (polys[k].iscontained && polys[k].containedbyid == i && polys[k].polygon->contains(polys[j].polygon.get()))
                            {
                                istoplevelafterall = 1;
                                break;
                            }
                        }
                        if (istoplevelafterall == 0)
                        {
                            polys[j].iscontained = true;
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
                if (polys[i].iscontained) continue;

                // List of holes for this top level polygon
                std::unique_ptr<std::vector<Geometry*> > interior(new std::vector<Geometry*>);
                for (unsigned j=i+1; j < totalpolys; ++j)
                {
                   if (polys[j].iscontained && polys[j].containedbyid == i)
                   {
                       interior->push_back(polys[j].ring.release());
                   }
                }

                Polygon* poly(gf.createPolygon(polys[i].ring.release(), interior.release()));
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

                if ((excludepoly == 0) || (multipoly->isValid())) {
                    wkbs.emplace_back(multipoly.get(), true, projection);
                }
            }
            else
            {
                for(unsigned i=0; i<toplevelpolygons; i++) {
                    geom_ptr poly(polygons->at(i));
                    if (!poly->isValid() && !excludepoly) {
                        poly = geom_ptr(poly->buffer(0));
                        poly->normalize();
                    }
                    if ((excludepoly == 0) || (poly->isValid())) {
                        wkbs.emplace_back(poly.get(), true, projection);
                    }
                }
            }
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

    return wkbs;
}

geometry_builder::pg_geom_t geometry_builder::build_multilines(const multinodelist_t &xnodes, osmid_t osm_id) const
{
    pg_geom_t wkb;

    try
    {
        GeometryFactory gf;
        geom_ptr mline = create_multi_line(gf, xnodes);

        wkb.set(mline.get(), false);
    }//TODO: don't show in message id when osm_id == -1
    catch (const std::exception& e)
    {
        std::cerr << std::endl << "Standard exception processing way_id="<< osm_id << ": " << e.what()  << std::endl;
    }
    catch (...)
    {
        std::cerr << std::endl << "Exception caught processing way id=" << osm_id << std::endl;
    }
    return wkb;
}

geometry_builder::pg_geoms_t geometry_builder::build_both(const multinodelist_t &xnodes,
                                                            int make_polygon, int enable_multi,
                                                            double split_at, osmid_t osm_id) const
{
    pg_geoms_t wkbs;

    try
    {
        GeometryFactory gf;
        geom_ptr mline = create_multi_line(gf, xnodes);
        //geom_ptr noded (segment->Union(mline.get()));
        LineMerger merger;
        //merger.add(noded.get());
        merger.add(mline.get());
        std::unique_ptr<std::vector<LineString *> > merged(merger.getMergedLineStrings());

        // Procces ways into lines or simple polygon list
        std::vector<polygondata> polys;
        polys.reserve(merged->size());

        for (auto *line: *merged) {
            // stuff into unique pointer to ensure auto-destruct
            std::unique_ptr<LineString> pline(line);
            if (make_polygon && pline->getNumPoints() > 3 && pline->isClosed()) {
                std::unique_ptr<Polygon> poly(gf.createPolygon(gf.createLinearRing(pline->getCoordinates()),0));
                double area = get_area(poly.get(), projection);
                if (area > 0.0) {
                    polys.emplace_back(std::move(poly),
                                       gf.createLinearRing(pline->getCoordinates()),
                                       area);
                }
            } else {
                double distance = 0;
                std::unique_ptr<CoordinateSequence> segment;
                segment = std::unique_ptr<CoordinateSequence>(gf.getCoordinateSequenceFactory()->create((size_t)0, (size_t)2));
                segment->add(pline->getCoordinateN(0));
                for(int j=1; j<(int)pline->getNumPoints(); ++j) {
                    segment->add(pline->getCoordinateN(j));
                    distance += pline->getCoordinateN(j).distance(pline->getCoordinateN(j-1));
                    if ((distance >= split_at) || (j == (int)pline->getNumPoints()-1)) {
                        geom_ptr geom = geom_ptr(gf.createLineString(segment.release()));

                        wkbs.emplace_back(geom.get(), false);

                        segment.reset(gf.getCoordinateSequenceFactory()->create((size_t)0, (size_t)2));
                        distance=0;
                        segment->add(pline->getCoordinateN(j));
                    }
                }
            }
        }

        if (!polys.empty())
        {
            std::sort(polys.begin(), polys.end(), polygondata_comparearea());

            unsigned toplevelpolygons = 0;
            int istoplevelafterall;
            size_t totalpolys = polys.size();

            geos::geom::prep::PreparedGeometryFactory pgf;
            for (unsigned i=0 ;i < totalpolys; ++i)
            {
                if (polys[i].iscontained) continue;
                toplevelpolygons++;
                const geos::geom::prep::PreparedGeometry* preparedtoplevelpolygon = pgf.create(polys[i].polygon.get());

                for (unsigned j=i+1; j < totalpolys; ++j)
                {
                    // Does preparedtoplevelpolygon contain the smaller polygon[j]?
                    if (polys[j].containedbyid == 0 && preparedtoplevelpolygon->contains(polys[j].polygon.get()))
                    {
                        // are we in a [i] contains [k] contains [j] situation
                        // which would actually make j top level
                        istoplevelafterall = 0;
                        for (unsigned k=i+1; k < j; ++k)
                        {
                            if (polys[k].iscontained && polys[k].containedbyid == i && polys[k].polygon->contains(polys[j].polygon.get()))
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
                            polys[j].iscontained = true;
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
                if (polys[i].iscontained) continue;

                // List of holes for this top level polygon
                std::unique_ptr<std::vector<Geometry*> > interior(new std::vector<Geometry*>);
                for (unsigned j=i+1; j < totalpolys; ++j)
                {
                   if (polys[j].iscontained && polys[j].containedbyid == i)
                   {
                       interior->push_back(polys[j].ring.release());
                   }
                }

                Polygon* poly(gf.createPolygon(polys[i].ring.release(), interior.release()));
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

                if ((excludepoly == 0) || (multipoly->isValid())) {
                    wkbs.emplace_back(multipoly.get(), true, projection);
                }
            }
            else
            {
                for(unsigned i=0; i<toplevelpolygons; i++)
                {
                    geom_ptr poly(polygons->at(i));
                    if (!poly->isValid() && !excludepoly) {
                        poly = geom_ptr(poly->buffer(0));
                        poly->normalize();
                    }
                    if (!excludepoly || (poly->isValid())) {
                        wkbs.emplace_back(poly.get(), true, projection);
                    }
                }
            }
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

    return wkbs;
}
