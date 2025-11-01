/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2025 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "geom-from-osm.hpp"

#include "geom-area-assembler.hpp"
#include "osmtypes.hpp"

#include <osmium/osm/way.hpp>

#include <cassert>
#include <utility>

namespace geom {

void create_point(geometry_t *geom, osmium::Location const &location)
{
    if (location.valid()) {
        auto &point = geom->set<point_t>();
        point.set_x(location.lon());
        point.set_y(location.lat());
    }
}

void create_point(geometry_t *geom, osmium::Node const &node)
{
    create_point(geom, node.location());
}

geometry_t create_point(osmium::Node const &node)
{
    return geometry_t{point_t{node.location()}};
}

namespace {

/**
 * Fill point list with locations from nodes list. Consecutive identical
 * locations are collapsed into a single point.
 *
 * Returns true if the result is a valid linestring, i.e. it has more than
 * one point.
 */
bool fill_point_list(point_list_t *list, osmium::NodeRefList const &nodes)
{
    osmium::Location last{};

    list->reserve(nodes.size());
    for (auto const &node : nodes) {
        auto const loc = node.location();
        if (loc.valid() && loc != last) {
            list->emplace_back(loc);
            last = loc;
        }
    }

    return list->size() > 1;
}

void fill_polygon(polygon_t *polygon, osmium::Area const &area,
                  osmium::OuterRing const &outer_ring)
{
    assert(polygon->inners().empty());

    for (auto const &nr : outer_ring) {
        polygon->outer().emplace_back(nr.location());
    }

    for (auto const &inner_ring : area.inner_rings(outer_ring)) {
        auto &ring = polygon->inners().emplace_back();
        for (auto const &nr : inner_ring) {
            ring.emplace_back(nr.location());
        }
    }
}

} // anonymous namespace

void create_linestring(geometry_t *geom, osmium::Way const &way)
{
    auto &line = geom->set<linestring_t>();

    if (!fill_point_list(&line, way.nodes())) {
        geom->reset();
    }
}

geometry_t create_linestring(osmium::Way const &way)
{
    geometry_t geom{};
    create_linestring(&geom, way);
    return geom;
}

void create_polygon(geometry_t *geom, osmium::Way const &way,
                    osmium::memory::Buffer *area_buffer)
{
    auto &polygon = geom->set<polygon_t>();

    // A closed way with less than 4 nodes can never be a valid polygon
    if (way.nodes().size() < 4U) {
        geom->reset();
        return;
    }

    geom::area_assembler_t assembler{area_buffer};

    if (!assembler(way)) {
        geom->reset();
        return;
    }

    auto const &area = assembler.get_area();
    auto const &ring = *area.cbegin<osmium::OuterRing>();

    fill_point_list(&polygon.outer(), ring);
}

geometry_t create_polygon(osmium::Way const &way,
                          osmium::memory::Buffer *area_buffer)
{
    geometry_t geom{};
    create_polygon(&geom, way, area_buffer);
    return geom;
}

void create_multipoint(geometry_t *geom, osmium::memory::Buffer const &buffer)
{
    auto nodes = buffer.select<osmium::Node>();
    if (nodes.size() == 1) {
        auto const location = nodes.cbegin()->location();
        if (location.valid()) {
            geom->set<point_t>() = point_t{location};
        } else {
            geom->reset();
        }
    } else {
        auto &multipoint = geom->set<multipoint_t>();
        for (auto const &node : nodes) {
            auto const location = node.location();
            if (location.valid()) {
                multipoint.add_geometry(point_t{location});
            }
        }
        if (multipoint.num_geometries() == 0) {
            geom->reset();
        }

        // In the (unlikely) event that this multipoint geometry only contains
        // a single point because locations for all others were not available
        // turn it into a point geometry retroactively.
        if (multipoint.num_geometries() == 1) {
            auto const p = multipoint[0];
            geom->set<point_t>() = p;
        }
    }
}

geometry_t create_multipoint(osmium::memory::Buffer const &buffer)
{
    geometry_t geom{};
    create_multipoint(&geom, buffer);
    return geom;
}

void create_multilinestring(geometry_t *geom,
                            osmium::memory::Buffer const &buffer,
                            bool force_multi)
{
    auto ways = buffer.select<osmium::Way>();
    if (ways.size() == 1 && !force_multi) {
        auto &line = geom->set<linestring_t>();
        auto const &way = *ways.cbegin();
        if (!fill_point_list(&line, way.nodes())) {
            geom->reset();
        }
    } else {
        auto &multiline = geom->set<multilinestring_t>();
        for (auto const &way : ways) {
            linestring_t line;
            if (fill_point_list(&line, way.nodes())) {
                multiline.add_geometry(std::move(line));
            }
        }
        if (multiline.num_geometries() == 0) {
            geom->reset();
        }

        // In the (unlikely) event that this multilinestring geometry only
        // contains a single linestring because ways or locations for all
        // others were not available turn it into a linestring geometry
        // retroactively.
        if (multiline.num_geometries() == 1 && !force_multi) {
            // This has to be done in two steps, because the set<>()
            // destroys the content of multiline.
            auto p = std::move(multiline[0]);
            geom->set<linestring_t>() = std::move(p);
        }
    }
}

geometry_t create_multilinestring(osmium::memory::Buffer const &buffer,
                                  bool force_multi)
{
    geometry_t geom{};
    create_multilinestring(&geom, buffer, force_multi);
    return geom;
}

void create_multipolygon(geometry_t *geom, osmium::Relation const &relation,
                         osmium::memory::Buffer const &buffer,
                         osmium::memory::Buffer *area_buffer)
{
    geom::area_assembler_t assembler{area_buffer};

    if (!assembler(relation, buffer)) {
        geom->reset();
        return;
    }

    auto const &area = assembler.get_area();

    if (area.is_multipolygon()) {
        auto &multipolygon = geom->set<multipolygon_t>();

        for (auto const &outer : area.outer_rings()) {
            auto &polygon = multipolygon.add_geometry();
            fill_polygon(&polygon, area, outer);
        }
    } else {
        auto &polygon = geom->set<polygon_t>();
        fill_polygon(&polygon, area, *area.outer_rings().cbegin());
    }
}

geometry_t create_multipolygon(osmium::Relation const &relation,
                               osmium::memory::Buffer const &buffer,
                               osmium::memory::Buffer *area_buffer)
{
    geometry_t geom{};
    create_multipolygon(&geom, relation, buffer, area_buffer);
    return geom;
}

void create_collection(geometry_t *geom, osmium::memory::Buffer const &buffer)
{
    auto &collection = geom->set<collection_t>();

    for (auto const &obj : buffer) {
        if (obj.type() == osmium::item_type::node) {
            auto const &node = static_cast<osmium::Node const &>(obj);
            if (node.location().valid()) {
                collection.add_geometry(create_point(node));
            }
        } else if (obj.type() == osmium::item_type::way) {
            auto const &way = static_cast<osmium::Way const &>(obj);
            geometry_t item;
            auto &line = item.set<linestring_t>();
            if (fill_point_list(&line, way.nodes())) {
                collection.add_geometry(std::move(item));
            }
        }
    }

    if (collection.num_geometries() == 0) {
        geom->reset();
    }
}

geometry_t create_collection(osmium::memory::Buffer const &buffer)
{
    geometry_t geom{};
    create_collection(&geom, buffer);
    return geom;
}

} // namespace geom
