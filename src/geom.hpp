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

#include <algorithm>
#include <cassert>
#include <cmath>
#include <initializer_list>
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

private:
    double m_x = 0.0;
    double m_y = 0.0;

}; // class point_t

/// This type is used as the basis for linestrings and rings.
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
    using iterator = typename std::vector<GEOM>::const_iterator;
    using value_type = GEOM;

    [[nodiscard]] std::size_t num_geometries() const noexcept
    {
        return m_geometry.size();
    }

    GEOM &add_geometry(GEOM &&geom)
    {
        m_geometry.push_back(std::move(geom));
        return m_geometry.back();
    }

    [[nodiscard]] GEOM &add_geometry() { return m_geometry.emplace_back(); }

    [[nodiscard]] friend bool operator==(multigeometry_t const &a,
                                         multigeometry_t const &b) noexcept
    {
        return a.m_geometry == b.m_geometry;
    }

    [[nodiscard]] friend bool operator!=(multigeometry_t const &a,
                                         multigeometry_t const &b) noexcept
    {
        return a.m_geometry != b.m_geometry;
    }

    const_iterator begin() const noexcept { return m_geometry.cbegin(); }
    const_iterator end() const noexcept { return m_geometry.cend(); }
    const_iterator cbegin() const noexcept { return m_geometry.cbegin(); }
    const_iterator cend() const noexcept { return m_geometry.cend(); }

    GEOM const &operator[](std::size_t i) const noexcept
    {
        return m_geometry[i];
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

    template <typename T>
    constexpr explicit geometry_t(T geom, int srid = 4326)
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

    [[nodiscard]] friend bool operator==(geometry_t const &a,
                                         geometry_t const &b) noexcept
    {
        return (a.srid() == b.srid()) && (a.m_geom == b.m_geom);
    }

    [[nodiscard]] friend bool operator!=(geometry_t const &a,
                                         geometry_t const &b) noexcept
    {
        return !(a == b);
    }

private:
    std::variant<nullgeom_t, point_t, linestring_t, polygon_t, multipoint_t,
                 multilinestring_t, multipolygon_t, collection_t>
        m_geom = nullgeom_t{};

    int m_srid = 4326;

}; // class geometry_t

} // namespace geom

// This magic is used for visiting geometries. For an explanation see for
// instance here:
// https://arne-mertz.de/2018/05/overload-build-a-variant-visitor-on-the-fly/
template <class... Ts>
struct overloaded : Ts...
{
    using Ts::operator()...;
};

template <class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

#endif // OSM2PGSQL_GEOM_HPP
