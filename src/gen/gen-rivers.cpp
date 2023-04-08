/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "gen-rivers.hpp"

#include "geom-functions.hpp"
#include "logging.hpp"
#include "params.hpp"
#include "pgsql.hpp"
#include "util.hpp"
#include "wkb.hpp"

#include <algorithm>
#include <cassert>
#include <map>
#include <unordered_map>
#include <vector>

gen_rivers_t::gen_rivers_t(pg_conn_t *connection, params_t *params)
: gen_base_t(connection, params), m_timer_area(add_timer("area")),
  m_timer_prep(add_timer("prep")), m_timer_get(add_timer("get")),
  m_timer_sort(add_timer("sort")), m_timer_net(add_timer("net")),
  m_timer_remove(add_timer("remove")), m_timer_width(add_timer("width")),
  m_timer_write(add_timer("write")),
  m_delete_existing(params->has("delete_existing"))
{
    check_src_dest_table_params_exist();

    params->check_identifier_with_default("src_areas", "waterway_areas");
    params->check_identifier_with_default("id_column", "way_id");
    params->check_identifier_with_default("width_column", "width");
    params->check_identifier_with_default("name_column", "name");

    params->set("qualified_src_areas",
                qualified_name(get_params().get_string("schema"),
                               get_params().get_string("src_areas")));
}

/// The data for a graph edge in the waterway network.
struct edge_t
{
    // All the points in this edge
    geom::linestring_t points;

    // Edges can be made from (part) of one or more OSM ways, this is the id
    // of one of them.
    osmid_t id = 0;

    // The width of the river along this edge
    double width = 0.0;
};

bool operator<(edge_t const &a, edge_t const &b) noexcept
{
    assert(a.points.size() > 1 && b.points.size() > 1);
    if (a.points[0] == b.points[0]) {
        return a.points[1] < b.points[1];
    }
    return a.points[0] < b.points[0];
}

bool operator<(edge_t const &a, geom::point_t b) noexcept
{
    assert(!a.points.empty());
    return a.points[0] < b;
}

bool operator<(geom::point_t a, edge_t const &b) noexcept
{
    assert(!b.points.empty());
    return a < b.points[0];
}

static void
follow_chain_and_set_width(edge_t const &edge, std::vector<edge_t> *edges,
                           std::map<geom::point_t, uint8_t> const &node_order,
                           geom::linestring_t *seen)
{
    assert(!edge.points.empty());

    auto const seen_it =
        std::find(seen->cbegin(), seen->cend(), edge.points[0]);
    if (seen_it != seen->cend()) {
        return; // loop detected
    }

    seen->push_back(edge.points[0]);

    assert(edge.points.size() > 1);
    auto const next_point = edge.points.back();
    if (node_order.at(next_point) > 1) {
        auto const [s, e] =
            std::equal_range(edges->begin(), edges->end(), next_point);

        if (std::next(s) == e) {
            if (s->width < edge.width) {
                s->width = edge.width;
                follow_chain_and_set_width(*s, edges, node_order, seen);
            }
        } else {
            for (auto it = s; it != e; ++it) {
                assert(it->points[0] == next_point);
                if (it->width < edge.width) {
                    it->width = edge.width;
                    auto seen2 = *seen;
                    follow_chain_and_set_width(*it, edges, node_order, &seen2);
                }
            }
        }
    }
}

static void assemble_edge(edge_t *edge, std::vector<edge_t> *edges,
                          std::map<geom::point_t, uint8_t> const &node_order)

{
    assert(edge);
    assert(edges);
    while (true) {
        assert(edge->points.size() > 1);
        geom::point_t const next_point = edge->points.back();

        auto const count = node_order.at(next_point);
        if (count != 2) {
            return;
        }

        auto const [s, e] =
            std::equal_range(edges->begin(), edges->end(), next_point);

        if (s == e) {
            return;
        }
        assert(e == std::next(s));

        auto const it = s;
        if (it->points.size() == 1 || &*it == edge) {
            return;
        }

        if (it->points[0] != next_point) {
            return;
        }
        assert(it != edges->end());

        edge->width = std::max(edge->width, it->width);

        if (it->points.size() == 2) {
            edge->points.push_back(it->points.back());
            it->points.resize(1);
            it->points.shrink_to_fit();
        } else {
            edge->points.insert(edge->points.end(),
                                std::next(it->points.begin()),
                                it->points.end());
            it->points.resize(1);
            it->points.shrink_to_fit();
            return;
        }
    }
}

/// Get some stats from source table
void gen_rivers_t::get_stats()
{
    auto const result =
        dbexec("SELECT count(*), sum(ST_NumPoints(geom)) FROM {src}");

    m_num_waterways = strtoul(result.get_value(0, 0), nullptr, 10);
    m_num_points = strtoul(result.get_value(0, 1), nullptr, 10);

    log_gen("Found {} waterways with {} points.", m_num_waterways,
            m_num_points);
}

static std::string const &
get_name(std::unordered_map<osmid_t, std::string> const &names, osmid_t id)
{
    static std::string const empty;
    auto const it = names.find(id);
    if (it == names.end()) {
        return empty;
    }
    return it->second;
}

void gen_rivers_t::process()
{
    log_gen("Calculate waterway area width...");
    timer(m_timer_area).start();
    dbexec(R"(UPDATE {qualified_src_areas} SET width =)"
           R"( (ST_MaximumInscribedCircle("{geom_column}")).radius * 2)"
           R"( WHERE width IS NULL)");
    dbexec("ANALYZE {qualified_src_areas}");
    timer(m_timer_area).stop();

    log_gen("Get 'width' from areas onto lines...");
    timer(m_timer_prep).start();
    dbexec(R"(
WITH _covered_lines AS (
    SELECT "{geom_column}" AS geom, "{id_column}" AS wid FROM {src} w
        WHERE ST_NumPoints(w."{geom_column}") > 2 AND ST_CoveredBy(w."{geom_column}",
            (SELECT ST_Union("{geom_column}") FROM {qualified_src_areas} a
                WHERE ST_Intersects(w."{geom_column}", a."{geom_column}")))
), _intersections AS (
    SELECT w.wid, ST_Intersection(a.geom, w.geom) AS inters,
           ST_Length(w.geom) AS wlength, a.width AS width
        FROM _covered_lines w, {qualified_src_areas} a
        WHERE ST_Intersects(w.geom, a.geom)
), _lines AS (
    SELECT wid, wlength, ST_Length(inters) * width AS lenwidth FROM _intersections
        WHERE ST_GeometryType(inters) IN ('ST_LineString', 'ST_MultiLineString')
), _glines AS (
    SELECT wid, sum(lenwidth) / wlength AS width FROM _lines
    GROUP BY wid, wlength
)
UPDATE {src} a SET width = l.width
    FROM _glines l WHERE l.wid = a."{id_column}" AND a.width IS NULL
    )");
    timer(m_timer_prep).stop();

    log_gen("Reading waterway lines from database...");
    get_stats();

    // This vector will initially contain all segments (connection between
    // two points) from waterway ways. They will later be assembled into
    // graph edges connecting points where the waterways network branches.
    std::vector<edge_t> edges;
    edges.reserve(m_num_points - m_num_waterways);

    // This stores the order of each node in our graph, i.e. the number of
    // connections this node has. Order 1 are beginning or end of a waterway,
    // order 2 is just the continuing waterway, order >= 3 is a branching
    // point.
    std::map<geom::point_t, uint8_t> node_order;

    // This is where we keep the names of all waterways indexed by their
    // way id.
    std::unordered_map<osmid_t, std::string> names;

    timer(m_timer_get).start();
    {
        auto const result = dbexec(R"(
SELECT "{id_column}", "{width_column}", "{name_column}", "{geom_column}"
 FROM {src};
)");

        for (int i = 0; i < result.num_tuples(); ++i) {
            auto const id = std::strtol(result.get_value(i, 0), nullptr, 10);
            auto const width = std::strtod(result.get_value(i, 1), nullptr);
            auto const name = result.get(i, 2);
            if (!name.empty()) {
                names.emplace(id, name);
            }
            auto const geom = ewkb_to_geom(decode_hex(result.get_value(i, 3)));

            if (geom.is_linestring()) {
                auto const &ls = geom.get<geom::linestring_t>();
                geom::for_each_segment(ls,
                                       [&](geom::point_t a, geom::point_t b) {
                                           if (a != b) {
                                               auto &f = edges.emplace_back();
                                               f.points.push_back(a);
                                               f.points.push_back(b);
                                               f.id = id;
                                               f.width = width;
                                               node_order[a]++;
                                               node_order[b]++;
                                           }
                                       });
            }
        }
    }
    timer(m_timer_get).stop();
    log_gen("Read {} segments, {} unique points, and {} names.", edges.size(),
            node_order.size(), names.size());

    if (edges.size() < 2) {
        log_gen("Found fewer than two segments. Nothing to do.");
        return;
    }

    log_gen("Sorting segments...");
    timer(m_timer_sort).start();
    std::sort(edges.begin(), edges.end());
    timer(m_timer_sort).stop();

    log_gen("Assembling edges from segments...");
    timer(m_timer_net).start();
    for (auto &edge : edges) {
        if (edge.points.size() > 1) {
            assemble_edge(&edge, &edges, node_order);
        }
    }
    timer(m_timer_net).stop();

    log_gen("Removing now empty edges...");
    timer(m_timer_remove).start();
    {
        auto const last =
            std::remove_if(edges.begin(), edges.end(), [](edge_t const &edge) {
                return edge.points.size() == 1;
            });
        edges.erase(last, edges.end());
        std::sort(edges.begin(), edges.end());
    }
    timer(m_timer_remove).stop();

    log_gen("Network has {} edges.", edges.size());

    log_gen("Propagating 'width' property downstream...");
    timer(m_timer_width).start();
    for (auto &edge : edges) {
        assert(!edge.points.empty());
        geom::linestring_t seen;
        follow_chain_and_set_width(edge, &edges, node_order, &seen);
    }
    timer(m_timer_width).stop();

    if (m_delete_existing) {
        dbexec("TRUNCATE {dest}");
    }

    log_gen("Writing results to destination table...");
    dbexec("PREPARE ins (int8, real, text, geometry) AS"
           " INSERT INTO {dest} ({id_column}, width, name, geom)"
           " VALUES ($1, $2, $3, $4)");

    timer(m_timer_write).start();
    connection().exec("BEGIN");
    for (auto &edge : edges) {
        geom::geometry_t const geom{std::move(edge.points), 3857};
        auto const wkb = geom_to_ewkb(geom);
        connection().exec_prepared("ins", edge.id, edge.width,
                                   get_name(names, edge.id), binary_param(wkb));
    }
    connection().exec("COMMIT");
    timer(m_timer_write).stop();

    dbexec("ANALYZE {dest}");

    log_gen("Done.");
}
