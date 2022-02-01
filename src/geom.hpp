#ifndef OSM2PGSQL_GEOM_HPP
#define OSM2PGSQL_GEOM_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

/**
 * \file
 *
 * Basic geometry types and functions.
 */

#include <osmium/osm/location.hpp>

#include <cassert>
#include <cmath>
#include <initializer_list>
#include <utility>
#include <variant>
#include <vector>

namespace geom {

class nullgeom_t
{
}; // class nullgeom_t

class point_t
{
public:
    point_t() = default;

    explicit point_t(osmium::Location location) noexcept
    : m_x(location.lon_without_check()), m_y(location.lat_without_check())
    {}

    constexpr point_t(double x, double y) noexcept : m_x(x), m_y(y) {}

    constexpr double x() const noexcept { return m_x; }
    constexpr double y() const noexcept { return m_y; }

    constexpr void set_x(double value) noexcept { m_x = value; }
    constexpr void set_y(double value) noexcept { m_y = value; }

    constexpr friend bool operator==(point_t a, point_t b) noexcept
    {
        return a.x() == b.x() && a.y() == b.y();
    }

    constexpr friend bool operator!=(point_t a, point_t b) noexcept
    {
        return !(a == b);
    }

private:
    double m_x = 0.0;
    double m_y = 0.0;

}; // class point_t

/// This type is used as the basis for linestrings and rings.
using point_list_t = std::vector<point_t>;

bool operator==(point_list_t const &a, point_list_t const &b) noexcept;
bool operator!=(point_list_t const &a, point_list_t const &b) noexcept;

class linestring_t : public point_list_t
{
public:
    linestring_t() = default;

    template <typename Iterator>
    linestring_t(Iterator begin, Iterator end) : point_list_t(begin, end)
    {}

    linestring_t(std::initializer_list<point_t> list)
    : point_list_t(list.begin(), list.end())
    {}

}; // class linestring_t

class ring_t : public point_list_t
{
public:
    ring_t() = default;

    template <typename Iterator>
    ring_t(Iterator begin, Iterator end) : point_list_t(begin, end)
    {}

    ring_t(std::initializer_list<point_t> list)
    : point_list_t(list.begin(), list.end())
    {}

}; // class ring_t

class polygon_t
{
public:
    polygon_t() = default;

    explicit polygon_t(ring_t &&ring) : m_outer(std::move(ring)) {}

    ring_t const &outer() const noexcept { return m_outer; }

    ring_t &outer() noexcept { return m_outer; }

    std::vector<ring_t> const &inners() const noexcept { return m_inners; }

    std::vector<ring_t> &inners() noexcept { return m_inners; }

    void add_inner_ring(ring_t &&ring) { m_inners.push_back(std::move(ring)); }

private:
    ring_t m_outer;
    std::vector<ring_t> m_inners;

}; // class polygon_t

template <typename GEOM>
class multigeometry_t : public std::vector<GEOM>
{
public:
    using const_iterator = typename std::vector<GEOM>::const_iterator;

    std::size_t num_geometries() const noexcept { return this->size(); }

    void add_geometry(GEOM &&geom) { this->push_back(std::move(geom)); }

    GEOM &add_geometry() { return this->emplace_back(); }

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

    template <typename T>
    constexpr explicit geometry_t(T geom, int srid = 4326)
    : m_geom(std::move(geom)), m_srid(srid)
    {}

    constexpr int srid() const noexcept { return m_srid; }

    constexpr void set_srid(int srid) noexcept { m_srid = srid; }

    constexpr bool is_null() const noexcept
    {
        return std::holds_alternative<nullgeom_t>(m_geom);
    }
    constexpr bool is_point() const noexcept
    {
        return std::holds_alternative<point_t>(m_geom);
    }
    constexpr bool is_linestring() const noexcept
    {
        return std::holds_alternative<linestring_t>(m_geom);
    }
    constexpr bool is_polygon() const noexcept
    {
        return std::holds_alternative<polygon_t>(m_geom);
    }
    constexpr bool is_multipoint() const noexcept
    {
        return std::holds_alternative<multipoint_t>(m_geom);
    }
    constexpr bool is_multilinestring() const noexcept
    {
        return std::holds_alternative<multilinestring_t>(m_geom);
    }
    constexpr bool is_multipolygon() const noexcept
    {
        return std::holds_alternative<multipolygon_t>(m_geom);
    }
    constexpr bool is_collection() const noexcept
    {
        return std::holds_alternative<collection_t>(m_geom);
    }

    constexpr bool is_multi() const noexcept
    {
        return is_multipoint() || is_multilinestring() || is_multipolygon() ||
               is_collection();
    }

    template <typename T>
    constexpr T const &get() const
    {
        return std::get<T>(m_geom);
    }

    template <typename T>
    constexpr T &get()
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

private:
    std::variant<nullgeom_t, point_t, linestring_t, polygon_t, multipoint_t,
                 multilinestring_t, multipolygon_t, collection_t>
        m_geom = nullgeom_t{};

    int m_srid = 4326;

}; // class geometry_t

} // namespace geom

#endif // OSM2PGSQL_GEOM_HPP
