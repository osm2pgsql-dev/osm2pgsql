#ifndef OSM2PGSQL_GEOM_BOX_HPP
#define OSM2PGSQL_GEOM_BOX_HPP

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
 * Class and functions for creating bounding boxes of geometries.
 */

#include "geom.hpp"

#include <cassert>
#include <limits>

namespace geom {

class box_t
{
public:
    constexpr box_t() noexcept = default;

    constexpr box_t(double min_x, double min_y, double max_x,
                    double max_y) noexcept
    : m_min_x(min_x), m_min_y(min_y), m_max_x(max_x), m_max_y(max_y)
    {
        assert(min_x <= max_x);
        assert(min_y <= max_y);
    }

    box_t &extend(point_t const &point) noexcept;
    void extend(point_list_t const &list) noexcept;
    void extend(box_t const &box) noexcept;

    constexpr double min_x() const noexcept { return m_min_x; }
    constexpr double min_y() const noexcept { return m_min_y; }
    constexpr double max_x() const noexcept { return m_max_x; }
    constexpr double max_y() const noexcept { return m_max_y; }

    void set_min_x(double value) noexcept { m_min_x = value; }
    void set_min_y(double value) noexcept { m_min_y = value; }
    void set_max_x(double value) noexcept { m_max_x = value; }
    void set_max_y(double value) noexcept { m_max_y = value; }

    constexpr double width() const noexcept { return m_max_x - m_min_x; }
    constexpr double height() const noexcept { return m_max_y - m_min_y; }

    constexpr point_t center() const noexcept
    {
        return {m_min_x + width() / 2.0, m_min_y + height() / 2.0};
    }

    constexpr point_t min() const noexcept { return {m_min_x, m_min_y}; }
    constexpr point_t max() const noexcept { return {m_max_x, m_max_y}; }

    constexpr friend bool operator==(box_t const &a, box_t const &b)
    {
        return a.min_x() == b.min_x() && a.min_y() == b.min_y() &&
               a.max_x() == b.max_x() && a.max_y() == b.max_y();
    }

    constexpr friend bool operator!=(box_t const &a, box_t const &b)
    {
        return !(a == b);
    }

private:
    double m_min_x = std::numeric_limits<double>::max();
    double m_min_y = std::numeric_limits<double>::max();
    double m_max_x = std::numeric_limits<double>::lowest();
    double m_max_y = std::numeric_limits<double>::lowest();

}; // class box_t

box_t envelope(geom::nullgeom_t const & /*geom*/);
box_t envelope(geom::point_t const &geom);
box_t envelope(geom::linestring_t const &geom);
box_t envelope(geom::polygon_t const &geom);
box_t envelope(geom::multipoint_t const &geom);
box_t envelope(geom::multilinestring_t const &geom);
box_t envelope(geom::multipolygon_t const &geom);
box_t envelope(geom::collection_t const &geom);

/**
 * Calculate the envelope of a geometry.
 */
box_t envelope(geometry_t const &geom);

} // namespace geom

#endif // OSM2PGSQL_GEOM_BOX_HPP
