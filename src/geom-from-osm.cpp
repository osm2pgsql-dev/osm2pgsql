/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "geom-from-osm.hpp"
#include "osmtypes.hpp"

#include <osmium/area/geom_assembler.hpp>
#include <osmium/osm/way.hpp>

#include <utility>

namespace geom {

geometry_t create_point(osmium::Node const &node)
{
    return geometry_t{point_t{node.location()}};
}

static void fill_point_list(point_list_t *list,
                            osmium::NodeRefList const &nodes)
{
    osmium::Location last{};

    for (auto const &node : nodes) {
        auto const loc = node.location();
        if (loc.valid() && loc != last) {
            list->emplace_back(loc);
            last = loc;
        }
    }
}

geometry_t create_linestring(osmium::Way const &way)
{
    geometry_t geom{linestring_t{}};
    auto &line = geom.get<linestring_t>();

    fill_point_list(&line, way.nodes());

    // Return nullgeom_t if the line geometry is invalid
    if (line.size() <= 1U) {
        geom.reset();
    }

    return geom;
}

geometry_t create_polygon(osmium::Way const &way)
{
    geometry_t geom{polygon_t{}};

    // A closed way with less than 4 nodes can never be a valid polygon
    if (way.nodes().size() < 4U) {
        geom.reset();
        return geom;
    }

    osmium::area::AssemblerConfig area_config;
    area_config.ignore_invalid_locations = true;
    osmium::area::GeomAssembler assembler{area_config};
    osmium::memory::Buffer area_buffer{1024};

    if (!assembler(way, area_buffer)) {
        geom.reset();
        return geom;
    }

    auto const &area = area_buffer.get<osmium::Area>(0);
    auto const &ring = *area.begin<osmium::OuterRing>();

    fill_point_list(&geom.get<polygon_t>().outer(), ring);

    return geom;
}

geometry_t create_multilinestring(osmium::memory::Buffer const &ways)
{
    geometry_t geom{multilinestring_t{}};
    auto &mls = geom.get<multilinestring_t>();

    for (auto const &way : ways.select<osmium::Way>()) {
        linestring_t line;
        fill_point_list(&line, way.nodes());
        if (line.size() >= 2U) {
            mls.add_geometry(std::move(line));
        }
    }

    if (mls.num_geometries() == 0) {
        geom.reset();
    }

    return geom;
}

static void fill_polygon(polygon_t *polygon, osmium::Area const &area,
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

geometry_t create_multipolygon(osmium::Relation const &relation,
                               osmium::memory::Buffer const &way_buffer)
{
    geometry_t geom{};

    osmium::area::AssemblerConfig area_config;
    area_config.ignore_invalid_locations = true;
    osmium::area::GeomAssembler assembler{area_config};
    osmium::memory::Buffer area_buffer{1024};

    if (!assembler(relation, way_buffer, area_buffer)) {
        return geom;
    }

    auto const &area = area_buffer.get<osmium::Area>(0);

    if (area.is_multipolygon()) {
        auto &multipolygon = geom.set<multipolygon_t>();

        for (auto const &outer : area.outer_rings()) {
            auto &polygon = multipolygon.add_geometry();
            fill_polygon(&polygon, area, outer);
        }
    } else {
        auto &polygon = geom.set<polygon_t>();
        fill_polygon(&polygon, area, *area.outer_rings().begin());
    }

    return geom;
}

} // namespace geom
