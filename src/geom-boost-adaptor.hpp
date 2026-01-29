#ifndef OSM2PGSQL_GEOM_BOOST_ADAPTOR_HPP
#define OSM2PGSQL_GEOM_BOOST_ADAPTOR_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2026 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "geom.hpp"
#include "geom-box.hpp"

#include <boost/geometry.hpp>
#include <boost/geometry/geometries/register/box.hpp>
#include <boost/geometry/geometries/register/linestring.hpp>
#include <boost/geometry/geometries/register/multi_linestring.hpp>
#include <boost/geometry/geometries/register/multi_point.hpp>
#include <boost/geometry/geometries/register/multi_polygon.hpp>
#include <boost/geometry/geometries/register/point.hpp>
#include <boost/geometry/geometries/register/ring.hpp>

BOOST_GEOMETRY_REGISTER_POINT_2D_GET_SET(geom::point_t, double, cs::cartesian,
                                         x, y, set_x, set_y)
BOOST_GEOMETRY_REGISTER_LINESTRING(geom::linestring_t)
BOOST_GEOMETRY_REGISTER_RING(geom::ring_t)

BOOST_GEOMETRY_REGISTER_MULTI_POINT(geom::multipoint_t)
BOOST_GEOMETRY_REGISTER_MULTI_LINESTRING(geom::multilinestring_t)
BOOST_GEOMETRY_REGISTER_MULTI_POLYGON(geom::multipolygon_t)

namespace boost::geometry::traits {
template <>
struct point_order<::geom::ring_t>
{
    // NOLINTNEXTLINE(readability-identifier-naming) - not in our namespace
    static order_selector const value = counterclockwise;
};

template <>
struct tag<::geom::polygon_t>
{
    using type = polygon_tag;
};
template <>
struct ring_const_type<::geom::polygon_t>
{
    using type = ::geom::ring_t const &;
};
template <>
struct ring_mutable_type<::geom::polygon_t>
{
    using type = ::geom::ring_t &;
};
template <>
struct interior_const_type<::geom::polygon_t>
{
    using type = std::vector<::geom::ring_t> const &;
};
template <>
struct interior_mutable_type<::geom::polygon_t>
{
    using type = std::vector<::geom::ring_t> &;
};

template <>
struct exterior_ring<::geom::polygon_t>
{
    // NOLINTNEXTLINE(google-runtime-references)
    static auto &get(::geom::polygon_t &p) { return p.outer(); }
    static auto const &get(::geom::polygon_t const &p) { return p.outer(); }
};

template <>
struct interior_rings<::geom::polygon_t>
{
    // NOLINTNEXTLINE(google-runtime-references)
    static auto &get(::geom::polygon_t &p) { return p.inners(); }
    static auto const &get(::geom::polygon_t const &p) { return p.inners(); }
};

BOOST_GEOMETRY_DETAIL_SPECIALIZE_BOX_TRAITS(::geom::box_t, ::geom::point_t)

template <>
struct indexed_access<::geom::box_t, min_corner, 0>
{
    static double get(::geom::box_t const &b) { return b.min_x(); }
    static void set(::geom::box_t &b, double value) { b.set_min_x(value); }
};

template <>
struct indexed_access<::geom::box_t, min_corner, 1>
{
    static double get(::geom::box_t const &b) { return b.min_y(); }
    static void set(::geom::box_t &b, double value) { b.set_min_y(value); }
};

template <>
struct indexed_access<::geom::box_t, max_corner, 0>
{
    static double get(::geom::box_t const &b) { return b.max_x(); }
    static void set(::geom::box_t &b, double value) { b.set_max_x(value); }
};

template <>
struct indexed_access<::geom::box_t, max_corner, 1>
{
    static double get(::geom::box_t const &b) { return b.max_y(); }
    static void set(::geom::box_t &b, double value) { b.set_max_y(value); }
};

} // namespace boost::geometry::traits

#endif // OSM2PGSQL_GEOM_BOOST_ADAPTOR_HPP
