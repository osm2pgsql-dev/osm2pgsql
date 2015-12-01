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

#ifndef GEOMETRY_BUILDER_H
#define GEOMETRY_BUILDER_H

#include "osmtypes.hpp"

#include <vector>
#include <string>
#include <memory>

namespace geos { namespace geom {
class Geometry;
class GeometryFactory;
class CoordinateSequence;
}}

struct reprojection;

class geometry_builder
{
public:
    struct pg_geom_t
    {
        pg_geom_t(const std::string &geom_str, bool poly, double geom_area = 0)
        : geom(geom_str), area(geom_area), polygon(poly)
        {}

        /// Create an invalid geometry.
        pg_geom_t()
        : area(0), polygon(false)
        {}

        pg_geom_t(const geos::geom::Geometry *g, bool poly, reprojection *p = nullptr)
        {
            set(g, poly, p);
        }

        /**
         * Set geometry from a Geos geometry.
         */
        void set(const geos::geom::Geometry *geom, bool poly, reprojection *p = nullptr);


        bool is_polygon() const
        {
            return polygon;
        }

        bool valid() const
        {
            return !geom.empty();
        }

        std::string geom;
        double area;
        bool polygon;
    };

    typedef std::vector<geometry_builder::pg_geom_t> pg_geoms_t;

    static int parse_wkb(const char *wkb, multinodelist_t &nodes, bool *polygon);
    pg_geom_t get_wkb_simple(const nodelist_t &nodes, int polygon) const;
    pg_geoms_t get_wkb_split(const nodelist_t &nodes, int polygon, double split_at) const;
    pg_geoms_t build_both(const multinodelist_t &xnodes, int make_polygon,
                            int enable_multi, double split_at, osmid_t osm_id = -1) const;
    pg_geoms_t build_polygons(const multinodelist_t &xnodes, bool enable_multi, osmid_t osm_id = -1) const;
    /** Output relation as a multiline.
     *
     *  Used by gazetteer only.
     */
    pg_geom_t build_multilines(const multinodelist_t &xnodes, osmid_t osm_id) const;

    void set_exclude_broken_polygon(bool exclude)
    {
        excludepoly = exclude;
    }

    void set_reprojection(reprojection *r)
    {
        projection = r;
    }


private:
    std::unique_ptr<geos::geom::Geometry>
    create_simple_poly(geos::geom::GeometryFactory &gf,
                       std::unique_ptr<geos::geom::CoordinateSequence> coords) const;

    bool excludepoly = false;
    reprojection *projection = nullptr;
};

#endif
