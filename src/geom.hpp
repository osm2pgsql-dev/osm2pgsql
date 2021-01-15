#ifndef OSM2PGSQL_GEOM_HPP
#define OSM2PGSQL_GEOM_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2020 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

/**
 * \file
 *
 * Low level geometry functions and types.
 */

#include "reprojection.hpp"

#include <osmium/geom/coordinates.hpp>
#include <osmium/osm/node_ref_list.hpp>

#include <initializer_list>
#include <ostream>
#include <vector>

namespace geom {

/// Calculate the Euclidean distance between two coordinates
double distance(osmium::geom::Coordinates p1,
                osmium::geom::Coordinates p2) noexcept;

osmium::geom::Coordinates interpolate(osmium::geom::Coordinates p1,
                                      osmium::geom::Coordinates p2,
                                      double frac) noexcept;

class linestring_t
{
public:
    linestring_t() = default;

    linestring_t(std::initializer_list<osmium::geom::Coordinates> coords);

    /**
     * Construct a linestring from a way node list. Nodes without location
     * are ignored. Consecutive nodes with the same location will only end
     * up once in the linestring.
     *
     * The resulting linestring will either be empty or have at least two
     * points in it.
     *
     * \param nodes The input nodes.
     * \param proj The projection used to project all coordinates.
     */
    linestring_t(osmium::NodeRefList const &nodes, reprojection const &proj);

    using iterator = std::vector<osmium::geom::Coordinates>::iterator;

    using const_iterator =
        std::vector<osmium::geom::Coordinates>::const_iterator;

    bool empty() const noexcept { return m_coordinates.empty(); }

    std::size_t size() const noexcept { return m_coordinates.size(); }

    void clear() noexcept { m_coordinates.clear(); }

    void add_point(osmium::geom::Coordinates coordinates)
    {
        m_coordinates.emplace_back(std::move(coordinates));
    }

    iterator begin() noexcept { return m_coordinates.begin(); }

    iterator end() noexcept { return m_coordinates.end(); }

    const_iterator begin() const noexcept { return m_coordinates.cbegin(); }

    const_iterator end() const noexcept { return m_coordinates.cend(); }

    const_iterator cbegin() const noexcept { return m_coordinates.cbegin(); }

    const_iterator cend() const noexcept { return m_coordinates.cend(); }

    osmium::geom::Coordinates &operator[](std::size_t n) noexcept
    {
        return m_coordinates[n];
    }

    osmium::geom::Coordinates operator[](std::size_t n) const noexcept
    {
        return m_coordinates[n];
    }

private:
    std::vector<osmium::geom::Coordinates> m_coordinates;

}; // class linestring_t

/// Output a linestring in WKT format. Used for debugging.
template <typename TChar, typename TTraits>
std::basic_ostream<TChar, TTraits> &
operator<<(std::basic_ostream<TChar, TTraits> &out, const linestring_t &line)
{
    out << "LINESTRING(";

    bool first = true;
    for (auto const coord : line) {
        if (first) {
            first = false;
        } else {
            out << ',';
        }
        out << coord.x << ' ' << coord.y;
    }

    return out << ')';
}

} // namespace geom

#endif // OSM2PGSQL_GEOM_HPP
