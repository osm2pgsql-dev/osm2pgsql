#ifndef OSM2PGSQL_GEOM_HPP
#define OSM2PGSQL_GEOM_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2025 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

/**
 * \file
 *
 * Basic geometry types and functions.
 */

#include "projection.hpp"

#include <osmium/osm/location.hpp>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <initializer_list>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace geom {

class nullgeom_t
{
public:
    [[nodiscard]] constexpr static std::size_t num_geometries() noexcept
    {
        return 0;
    }

    [[nodiscard]] constexpr friend bool operator==(nullgeom_t,
                                                   nullgeom_t) noexcept
    {
        return true;
    }

    [[nodiscard]] constexpr friend bool operator!=(nullgeom_t,
                                                   nullgeom_t) noexcept
    {
        return false;
    }

}; // class nullgeom_t

class point_t
{
public:
    point_t() = default;

    explicit point_t(osmium::Location location) noexcept
    : m_x(location.lon_without_check()), m_y(location.lat_without_check())
    {}

    constexpr point_t(double x, double y) noexcept : m_x(x), m_y(y) {}

    [[nodiscard]] constexpr static std::size_t num_geometries() noexcept
    {
        return 1;
    }

    [[nodiscard]] constexpr double x() const noexcept { return m_x; }
    [[nodiscard]] constexpr double y() const noexcept { return m_y; }

    constexpr void set_x(double value) noexcept { m_x = value; }
    constexpr void set_y(double value) noexcept { m_y = value; }

    [[nodiscard]] constexpr friend bool operator==(point_t a,
                                                   point_t b) noexcept
    {
        return a.x() == b.x() && a.y() == b.y();
    }

    [[nodiscard]] constexpr friend bool operator!=(point_t a,
                                                   point_t b) noexcept
    {
        return !(a == b);
    }

    /// Give points an (arbitrary) order.
    [[nodiscard]] constexpr friend bool operator<(point_t a, point_t b) noexcept
    {
        if (a.x() == b.x()) {
            return a.y() < b.y();
        }
        return a.x() < b.x();
    }

    [[nodiscard]] constexpr friend bool operator>(point_t a, point_t b) noexcept
    {
        if (a.x() == b.x()) {
            return a.y() > b.y();
        }
        return a.x() > b.x();
    }

private:
    double m_x = 0.0;
    double m_y = 0.0;

}; // class point_t

/**
 * This type is used as the basis for linestrings and rings.
 *
 * Point lists should not contain consecutive duplicate points. You can
 * use the remove_duplicates() function to remove them if needed. (OGC validity
 * only requires there to be at least two different points in a linestring, but
 * we are more strict here to make sure we don't have to handle that anomaly
 * later on.)
 */
class point_list_t : public std::vector<point_t>
{
public:
    point_list_t() = default;

    template <typename Iterator>
    point_list_t(Iterator begin, Iterator end)
    : std::vector<point_t>(begin, end)
    {}

    point_list_t(std::initializer_list<point_t> list)
    : std::vector<point_t>(list.begin(), list.end())
    {}

    /// Collapse consecutive identical points into a single point (in-place).
    void remove_duplicates();

}; // class point_list_t

class linestring_t : public point_list_t
{
public:
    using point_list_t::point_list_t;

    [[nodiscard]] constexpr static std::size_t num_geometries() noexcept
    {
        return 1;
    }

}; // class linestring_t

class ring_t : public point_list_t
{
public:
    using point_list_t::point_list_t;

}; // class ring_t

class polygon_t
{
public:
    polygon_t() = default;

    explicit polygon_t(ring_t &&ring) : m_outer(std::move(ring)) {}

    [[nodiscard]] constexpr static std::size_t num_geometries() noexcept
    {
        return 1;
    }

    [[nodiscard]] ring_t const &outer() const noexcept { return m_outer; }

    [[nodiscard]] ring_t &outer() noexcept { return m_outer; }

    [[nodiscard]] std::vector<ring_t> const &inners() const noexcept
    {
        return m_inners;
    }

    [[nodiscard]] std::vector<ring_t> &inners() noexcept { return m_inners; }

    void add_inner_ring(ring_t &&ring) { m_inners.push_back(std::move(ring)); }

    friend bool operator==(polygon_t const &a, polygon_t const &b) noexcept;

    friend bool operator!=(polygon_t const &a, polygon_t const &b) noexcept;

private:
    ring_t m_outer;
    std::vector<ring_t> m_inners;

}; // class polygon_t

template <typename GEOM>
class multigeometry_t
{
public:
    using const_iterator = typename std::vector<GEOM>::const_iterator;
    using iterator = typename std::vector<GEOM>::iterator;
    using value_type = GEOM;

    static constexpr bool FOR_POINT = std::is_same_v<GEOM, point_t>;

    [[nodiscard]] std::size_t num_geometries() const noexcept
    {
        return m_geometry.size();
    }

    GEOM &
    add_geometry(typename std::conditional_t<FOR_POINT, point_t, GEOM &&> geom)
    {
        m_geometry.push_back(std::forward<GEOM>(geom));
        return m_geometry.back();
    }

    [[nodiscard]] GEOM &add_geometry() { return m_geometry.emplace_back(); }

    [[nodiscard]] friend bool operator==(multigeometry_t const &a,
                                         multigeometry_t const &b)
    {
        return a.m_geometry == b.m_geometry;
    }

    [[nodiscard]] friend bool operator!=(multigeometry_t const &a,
                                         multigeometry_t const &b)
    {
        return a.m_geometry != b.m_geometry;
    }

    iterator begin() noexcept { return m_geometry.begin(); }
    iterator end() noexcept { return m_geometry.end(); }
    const_iterator begin() const noexcept { return m_geometry.cbegin(); }
    const_iterator end() const noexcept { return m_geometry.cend(); }
    const_iterator cbegin() const noexcept { return m_geometry.cbegin(); }
    const_iterator cend() const noexcept { return m_geometry.cend(); }

    GEOM const &operator[](std::size_t n) const noexcept
    {
        return m_geometry[n];
    }

    GEOM &operator[](std::size_t n) noexcept
    {
        return m_geometry[n];
    }

    void remove_last()
    {
        assert(!m_geometry.empty());
        m_geometry.pop_back();
    }

    void reserve(std::size_t size) { m_geometry.reserve(size); }

private:
    std::vector<GEOM> m_geometry;

}; // class multigeometry_t

using multipoint_t = multigeometry_t<point_t>;
using multilinestring_t = multigeometry_t<linestring_t>;
using multipolygon_t = multigeometry_t<polygon_t>;

class geometry_t;
using collection_t = multigeometry_t<geometry_t>;

/**
 * This is a variant type holding any one of the geometry types (including
 * nullgeom_t) and a SRID.
 */
class geometry_t
{
public:
    constexpr geometry_t() = default;

    // point_t is small and trivially copyable, no move needed like for the
    // other constructors.
    constexpr explicit geometry_t(point_t geom, int srid = PROJ_LATLONG)
    : m_geom(geom), m_srid(srid)
    {}

    constexpr explicit geometry_t(linestring_t &&geom, int srid = PROJ_LATLONG)
    : m_geom(std::move(geom)), m_srid(srid)
    {}

    constexpr explicit geometry_t(polygon_t &&geom, int srid = PROJ_LATLONG)
    : m_geom(std::move(geom)), m_srid(srid)
    {}

    constexpr explicit geometry_t(multipoint_t &&geom, int srid = PROJ_LATLONG)
    : m_geom(std::move(geom)), m_srid(srid)
    {}

    constexpr explicit geometry_t(multilinestring_t &&geom,
                                  int srid = PROJ_LATLONG)
    : m_geom(std::move(geom)), m_srid(srid)
    {}

    constexpr explicit geometry_t(multipolygon_t &&geom,
                                  int srid = PROJ_LATLONG)
    : m_geom(std::move(geom)), m_srid(srid)
    {}

    constexpr explicit geometry_t(collection_t &&geom, int srid = PROJ_LATLONG)
    : m_geom(std::move(geom)), m_srid(srid)
    {}

    [[nodiscard]] constexpr int srid() const noexcept { return m_srid; }

    constexpr void set_srid(int srid) noexcept { m_srid = srid; }

    [[nodiscard]] constexpr bool is_null() const noexcept
    {
        return std::holds_alternative<nullgeom_t>(m_geom);
    }
    [[nodiscard]] constexpr bool is_point() const noexcept
    {
        return std::holds_alternative<point_t>(m_geom);
    }
    [[nodiscard]] constexpr bool is_linestring() const noexcept
    {
        return std::holds_alternative<linestring_t>(m_geom);
    }
    [[nodiscard]] constexpr bool is_polygon() const noexcept
    {
        return std::holds_alternative<polygon_t>(m_geom);
    }
    [[nodiscard]] constexpr bool is_multipoint() const noexcept
    {
        return std::holds_alternative<multipoint_t>(m_geom);
    }
    [[nodiscard]] constexpr bool is_multilinestring() const noexcept
    {
        return std::holds_alternative<multilinestring_t>(m_geom);
    }
    [[nodiscard]] constexpr bool is_multipolygon() const noexcept
    {
        return std::holds_alternative<multipolygon_t>(m_geom);
    }
    [[nodiscard]] constexpr bool is_collection() const noexcept
    {
        return std::holds_alternative<collection_t>(m_geom);
    }

    [[nodiscard]] constexpr bool is_multi() const noexcept
    {
        return is_multipoint() || is_multilinestring() || is_multipolygon() ||
               is_collection();
    }

    template <typename T>
    [[nodiscard]] constexpr T const &get() const
    {
        return std::get<T>(m_geom);
    }

    template <typename T>
    [[nodiscard]] constexpr T &get()
    {
        return std::get<T>(m_geom);
    }

    void reset() { m_geom.emplace<nullgeom_t>(); }

    template <typename T>
    constexpr T &set()
    {
        return m_geom.emplace<T>();
    }

    template <typename V>
    constexpr auto visit(V &&visitor) const
    {
        return std::visit(std::forward<V>(visitor), m_geom);
    }

    // This is non-member function visit(), different than the member
    // function above, because we need to move the geometry into the function
    // which we can't do for a member function.
    template <typename V>
    friend auto visit(V &&visitor, geometry_t &&geom)
    {
        return std::visit(std::forward<V>(visitor), std::move(geom.m_geom));
    }

    [[nodiscard]] friend bool operator==(geometry_t const &a,
                                         geometry_t const &b)
    {
        return (a.srid() == b.srid()) && (a.m_geom == b.m_geom);
    }

    [[nodiscard]] friend bool operator!=(geometry_t const &a,
                                         geometry_t const &b)
    {
        return !(a == b);
    }

private:
    std::variant<nullgeom_t, point_t, linestring_t, polygon_t, multipoint_t,
                 multilinestring_t, multipolygon_t, collection_t>
        m_geom = nullgeom_t{};

    int m_srid = PROJ_LATLONG;

}; // class geometry_t

inline std::size_t dimension(nullgeom_t const &) noexcept { return 0; }
inline std::size_t dimension(point_t const &) noexcept { return 0; }
inline std::size_t dimension(linestring_t const &) noexcept { return 1; }
inline std::size_t dimension(polygon_t const &) noexcept { return 2; }
inline std::size_t dimension(multipoint_t const &) noexcept { return 0; }
inline std::size_t dimension(multilinestring_t const &) noexcept { return 1; }
inline std::size_t dimension(multipolygon_t const &) noexcept { return 2; }

std::size_t dimension(collection_t const &geom);

/**
 * Return the dimension of this geometry. This is:
 *
 * 0 - for null and point geometries
 * 1 - for (multi)linestring geometries
 * 2 - for (multi)polygon geometries
 *
 * For geometry collections this is the largest dimension of all its members.
 */
std::size_t dimension(geometry_t const &geom);

} // namespace geom

#endif // OSM2PGSQL_GEOM_HPP
