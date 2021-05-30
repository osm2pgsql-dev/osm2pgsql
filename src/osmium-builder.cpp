/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2021 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <cassert>
#include <vector>

#include <osmium/area/geom_assembler.hpp>

#include "geom.hpp"
#include "osmium-builder.hpp"

namespace geom {

void osmium_builder_t::wrap_in_multipolygon(
    osmium_builder_t::wkbs_t *geometries)
{
    assert(!geometries->empty());

    m_writer.multipolygon_start();
    for (auto const &p : *geometries) {
        m_writer.add_sub_geometry(p);
    }
    (*geometries)[0] = m_writer.multipolygon_finish(geometries->size());
    geometries->resize(1);
}

void osmium_builder_t::wrap_in_multipolygon(osmium_builder_t::wkb_t *geometry)
{
    m_writer.multipolygon_start();
    m_writer.add_sub_geometry(*geometry);
    *geometry = m_writer.multipolygon_finish(1);
}

osmium_builder_t::wkb_t
osmium_builder_t::get_wkb_node(osmium::Location const &loc) const
{
    return m_writer.make_point(m_proj->reproject(loc));
}

osmium_builder_t::wkbs_t
osmium_builder_t::get_wkb_line(osmium::WayNodeList const &nodes,
                               double split_at)
{
    std::vector<linestring_t> linestrings;
    geom::make_line(linestring_t{nodes, *m_proj}, split_at, &linestrings);

    wkbs_t ret;

    for (auto const &line : linestrings) {
        m_writer.linestring_start();
        for (auto const &coord : line) {
            m_writer.add_location(coord);
        }
        ret.push_back(m_writer.linestring_finish(line.size()));
    }

    return ret;
}

osmium_builder_t::wkb_t
osmium_builder_t::get_wkb_polygon(osmium::Way const &way)
{
    osmium::area::AssemblerConfig area_config;
    area_config.ignore_invalid_locations = true;
    osmium::area::GeomAssembler assembler{area_config};

    m_buffer.clear();
    if (!assembler(way, m_buffer)) {
        return wkb_t();
    }

    auto const wkbs = create_polygons(m_buffer.get<osmium::Area>(0));

    return wkbs.empty() ? wkb_t{} : wkbs[0];
}

osmium_builder_t::wkbs_t
osmium_builder_t::get_wkb_multipolygon(osmium::Relation const &rel,
                                       osmium::memory::Buffer const &ways,
                                       bool build_multigeoms, bool wrap_multi)
{
    osmium::area::AssemblerConfig area_config;
    area_config.ignore_invalid_locations = true;
    osmium::area::GeomAssembler assembler{area_config};

    m_buffer.clear();

    wkbs_t ret;
    if (assembler(rel, ways, m_buffer)) {
        auto const &area = m_buffer.get<osmium::Area>(0);

        // This returns a vector of one or more polygons
        ret = create_polygons(area);
        assert(!ret.empty());

        if (build_multigeoms) {
            if (ret.size() > 1 || wrap_multi) {
                wrap_in_multipolygon(&ret);
            }
        } else {
            if (wrap_multi) {
                for (auto &wkb : ret) {
                    // wrap each polygon into its own multipolygon
                    wrap_in_multipolygon(&wkb);
                }
            }
        }
    }
    return ret;
}

osmium_builder_t::wkbs_t
osmium_builder_t::get_wkb_multiline(osmium::memory::Buffer const &ways,
                                    double split_at)
{
    std::vector<linestring_t> linestrings;
    make_multiline(ways, split_at, *m_proj, &linestrings);

    wkbs_t ret;

    for (auto const &line : linestrings) {
        m_writer.linestring_start();
        for (auto const &coord : line) {
            m_writer.add_location(coord);
        }
        ret.push_back(m_writer.linestring_finish(line.size()));
    }

    if (split_at <= 0.0 && !ret.empty()) {
        auto const num_lines = ret.size();
        m_writer.multilinestring_start();
        for (auto const &line : ret) {
            m_writer.add_sub_geometry(line);
        }
        ret.clear();
        ret.push_back(m_writer.multilinestring_finish(num_lines));
    }

    return ret;
}

size_t osmium_builder_t::add_mp_points(osmium::NodeRefList const &nodes)
{
    size_t num_points = 0;
    osmium::Location last_location;
    for (auto const &node_ref : nodes) {
        if (node_ref.location().valid() &&
            last_location != node_ref.location()) {
            last_location = node_ref.location();
            m_writer.add_location(m_proj->reproject(last_location));
            ++num_points;
        }
    }

    return num_points;
}

osmium_builder_t::wkbs_t
osmium_builder_t::create_polygons(osmium::Area const &area)
{
    wkbs_t ret;

    try {
        size_t num_rings = 0;

        for (auto const &item : area) {
            if (item.type() == osmium::item_type::outer_ring) {
                auto const &ring = static_cast<osmium::OuterRing const &>(item);
                if (num_rings > 0) {
                    ret.push_back(m_writer.polygon_finish(num_rings));
                    num_rings = 0;
                }
                m_writer.polygon_start();
                m_writer.polygon_ring_start();
                auto const num_points = add_mp_points(ring);
                m_writer.polygon_ring_finish(num_points);
                ++num_rings;
            } else if (item.type() == osmium::item_type::inner_ring) {
                auto const &ring = static_cast<osmium::InnerRing const &>(item);
                m_writer.polygon_ring_start();
                auto const num_points = add_mp_points(ring);
                m_writer.polygon_ring_finish(num_points);
                ++num_rings;
            }
        }

        auto const wkb = m_writer.polygon_finish(num_rings);
        if (num_rings > 0) {
            ret.push_back(wkb);
        }

    } catch (osmium::geometry_error const &) { /* ignored */
    }

    return ret;
}

} // namespace geom
