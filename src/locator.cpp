/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2025 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "locator.hpp"

#include "geom-boost-adaptor.hpp"
#include "geom-box.hpp"
#include "geom-functions.hpp"
#include "hex.hpp"
#include "overloaded.hpp"
#include "pgsql-capabilities.hpp"
#include "pgsql.hpp"
#include "wkb.hpp"

#include <iterator>

void locator_t::add_region(std::string const &name, geom::box_t const &box)
{
    m_regions.emplace_back(name, box);
}

void locator_t::add_region(std::string const &name,
                           geom::geometry_t const &geom)
{
    if (geom.is_polygon()) {
        m_regions.emplace_back(name, geom.get<geom::polygon_t>());
        return;
    }

    if (geom.is_multipolygon()) {
        for (auto const &polygon : geom.get<geom::multipolygon_t>()) {
            m_regions.emplace_back(name, polygon);
        }
        return;
    }

    throw std::runtime_error{
        "Invalid geometry type: Need (multi)polygon for region."};
}

void locator_t::add_regions(pg_conn_t const &db_connection,
                            std::string const &query)
{
    log_debug("Querying database for locator '{}'...", name());
    auto const result = db_connection.exec(query);
    if (result.num_fields() != 2) {
        throw std::runtime_error{"Locator queries must return exactly two "
                                 "columns with the name and the geometry."};
    }

    if (!is_geometry_type(result.field_type(1))) {
        throw std::runtime_error{
            "Second column in Locator query results must be a geometry."};
    }

    for (int n = 0; n < result.num_tuples(); ++n) {
        std::string const name = result.get_value(n, 0);
        auto geometry = ewkb_to_geom(util::decode_hex(result.get(n, 1)));

        if (geometry.srid() == 4326) {
            add_region(name, geometry);
        } else {
            log_warn("Ignoring locator geometry that is not in WGS84 (4326)");
        }
    }
    log_info("Added {} regions to locator '{}'.", result.num_tuples(), name());
}

void locator_t::build_index()
{
    log_debug("Building index for locator '{}'", name());
    std::vector<idx_value_t> m_data;
    m_data.reserve(m_regions.size());

    std::size_t n = 0;
    for (auto const &region : m_regions) {
        m_data.emplace_back(region.box(), n++);
    }

    m_rtree.clear();
    m_rtree.insert(m_data.cbegin(), m_data.cend());
}

std::set<std::string> locator_t::all_intersecting(geom::geometry_t const &geom)
{
    if (m_rtree.size() < m_regions.size()) {
        build_index();
    }

    std::set<std::string> results;

    geom.visit(overloaded{[&](geom::nullgeom_t const & /*input*/) {},
                          [&](geom::collection_t const & /*input*/) {}, // TODO
                          [&](auto const &val) {
                              for (auto it = begin_intersects(val);
                                   it != end_query(); ++it) {
                                  auto const &region = m_regions[it->second];
                                  results.emplace(region.name());
                              }
                          }});

    return results;
}

std::string locator_t::first_intersecting(geom::geometry_t const &geom)
{
    if (m_rtree.size() < m_regions.size()) {
        build_index();
    }

    std::string result;

    geom.visit(overloaded{[&](geom::nullgeom_t const & /*input*/) {},
                          [&](geom::collection_t const & /*input*/) {}, // TODO
                          [&](auto const &val) {
                              auto const it = begin_intersects(val);
                              if (it != end_query()) {
                                  auto const &region = m_regions[it->second];
                                  result = region.name();
                              }
                          }});

    return result;
}
