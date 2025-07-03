#ifndef OSM2PGSQL_LOCATOR_HPP
#define OSM2PGSQL_LOCATOR_HPP

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
 * For the region_t and locator_t classes.
 */

#include "geom-boost-adaptor.hpp"
#include "geom-box.hpp"
#include "geom.hpp"
#include "logging.hpp"

#include <boost/geometry/index/rtree.hpp>

#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

class pg_conn_t;

namespace bgi = ::boost::geometry::index;

/**
 * A locator stores a number of regions. Each region has a name and a bounding
 * box or polygon geometry. The locator can then check efficiently which
 * regions a specified geometry is intersecting.
 *
 * Names don't have to be unique. Geometries of regions can overlap. In fact it
 * is best to subdivide larger polygons into smaller ones, because the
 * intersection will be much faster to calculate that way. This will
 * automatically lead to lots of small polygons with the same name.
 */
class locator_t
{
private:
    class region_t
    {
    public:
        region_t(std::string name, geom::box_t const &box)
        : m_name(std::move(name)), m_box(box),
          m_polygon(
              geom::polygon_t{geom::ring_t{{m_box.min_x(), m_box.min_y()},
                                           {m_box.max_x(), m_box.min_y()},
                                           {m_box.max_x(), m_box.max_y()},
                                           {m_box.min_x(), m_box.max_y()},
                                           {m_box.min_x(), m_box.min_y()}}})
        {
        }

        region_t(std::string name, geom::polygon_t const &polygon)
        : m_name(std::move(name)), m_box(envelope(polygon)), m_polygon(polygon)
        {
        }

        std::string const &name() const noexcept { return m_name; }

        geom::box_t const &box() const noexcept { return m_box; }

        geom::polygon_t const &polygon() const noexcept { return m_polygon; }

    private:
        std::string m_name;
        geom::box_t m_box;
        geom::polygon_t m_polygon;

    }; // class region_t

    std::string m_name;
    std::vector<region_t> m_regions;

    using idx_value_t = std::pair<geom::box_t, std::size_t>;

    using tree_t = bgi::rtree<idx_value_t, bgi::rstar<16>>;
    tree_t m_rtree;

    template <typename T>
    tree_t::const_query_iterator begin_intersects(T const &geom)
    {
        return m_rtree.qbegin(
            bgi::intersects(geom) && bgi::satisfies([&](idx_value_t const &v) {
                auto const &region = m_regions[v.second];
                return boost::geometry::intersects(region.polygon(), geom);
            }));
    }

    tree_t::const_query_iterator end_query() { return m_rtree.qend(); }

    void all_intersecting_visit(geom::geometry_t const &geom,
                                std::set<std::string> *results);

    void first_intersecting_visit(geom::geometry_t const &geom,
                                  std::string *result);

public:
    /// The name of this locator (for logging only)
    std::string const &name() const noexcept { return m_name; }

    /// Are there any regions stored in this locator?
    bool empty() const noexcept { return m_regions.empty(); }

    /// Return the number of regions stored in this locator.
    std::size_t size() const noexcept { return m_regions.size(); }

    /// Set the name of this locator.
    void set_name(std::string name) { m_name = std::move(name); }

    /// Add a bounding box as region.
    void add_region(std::string const &name, geom::box_t const &box);

    /**
     * Add a (multi)polygon as region.
     *
     * Throws an exception if geom is not a (multi)polygon.
     */
    void add_region(std::string const &name, geom::geometry_t const &geom);

    void add_regions(pg_conn_t const &db_connection, std::string const &query);

    /// Build index containing all regions.
    void build_index();

    /**
     * Find all regions intersecting the specified geometry. Returns a set
     * of (unique) names of those regions.
     *
     * Automatically calls build_index() if needed.
     */
    std::set<std::string> all_intersecting(geom::geometry_t const &geom);

    /**
     * Find a region intersecting the specified geometry. If there is more
     * than one, a random one will be returned. Returns the name of the region.
     *
     * Automatically calls build_index() if needed.
     */
    std::string first_intersecting(geom::geometry_t const &geom);

}; // class locator_t

#endif // OSM2PGSQL_LOCATOR_HPP
