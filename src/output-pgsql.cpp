/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

/* Implements the mid-layer processing for osm2pgsql
 * using several PostgreSQL tables
 *
 * This layer stores data read in from the planet.osm file
 * and is then read by the backend processing code to
 * emit the final geometry-enabled output formats
*/

#include <future>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "expire-tiles.hpp"
#include "geom-from-osm.hpp"
#include "geom-functions.hpp"
#include "logging.hpp"
#include "middle.hpp"
#include "options.hpp"
#include "osmtypes.hpp"
#include "output-pgsql.hpp"
#include "pgsql.hpp"
#include "reprojection.hpp"
#include "taginfo-impl.hpp"
#include "tagtransform.hpp"
#include "util.hpp"
#include "wildcmp.hpp"
#include "wkb.hpp"

static double calculate_area(bool reproject_area,
                             geom::geometry_t const &geom4326,
                             geom::geometry_t const &geom)
{
    if (reproject_area) {
        // XXX there is some overhead here always dynamically
        // creating the same projection object. Needs refactoring.
        auto const ogeom =
            geom::transform(geom4326, *reprojection::create_projection(3857));
        return geom::area(ogeom);
    }
    return geom::area(geom);
}

void output_pgsql_t::pgsql_out_way(osmium::Way const &way, taglist_t *tags,
                                   bool polygon, bool roads)
{
    if (polygon && way.is_closed()) {
        auto const geom = geom::create_polygon(way);
        auto const projected_geom = geom::transform(geom, *m_proj);

        auto const wkb = geom_to_ewkb(projected_geom);
        if (!wkb.empty()) {
            m_expire.from_geometry(projected_geom, way.id());
            if (m_enable_way_area) {
                double const area = calculate_area(
                    get_options()->reproject_area, geom, projected_geom);
                util::double_to_buffer tmp{area};
                tags->set("way_area", tmp.c_str());
            }
            m_tables[t_poly]->write_row(way.id(), *tags, wkb);
        }
    } else {
        double const split_at =
            get_options()->projection->target_latlon() ? 1 : 100 * 1000;
        auto const geoms = geom::split_multi(geom::segmentize(
            geom::transform(geom::create_linestring(way), *m_proj), split_at));
        for (auto const &sgeom : geoms) {
            m_expire.from_geometry(sgeom, way.id());
            auto const wkb = geom_to_ewkb(sgeom);
            m_tables[t_line]->write_row(way.id(), *tags, wkb);
            if (roads) {
                m_tables[t_roads]->write_row(way.id(), *tags, wkb);
            }
        }
    }
}

void output_pgsql_t::pending_way(osmid_t id)
{
    // Try to fetch the way from the DB
    m_buffer.clear();
    if (middle().way_get(id, &m_buffer)) {
        pgsql_delete_way_from_output(id);

        taglist_t outtags;
        bool polygon = false;
        bool roads = false;
        auto &way = m_buffer.get<osmium::Way>(0);
        if (!m_tagtransform->filter_tags(way, &polygon, &roads, &outtags)) {
            auto nnodes = middle().nodes_get_list(&(way.nodes()));
            if (nnodes > 1) {
                pgsql_out_way(way, &outtags, polygon, roads);
                return;
            }
        }
    }
}

void output_pgsql_t::pending_relation(osmid_t id)
{
    // Try to fetch the relation from the DB
    // Note that we cannot use the global buffer here because
    // we cannot keep a reference to the relation and an autogrow buffer
    // might be relocated when more data is added.
    m_rels_buffer.clear();
    if (middle().relation_get(id, &m_rels_buffer)) {
        pgsql_delete_relation_from_output(id);

        auto const &rel = m_rels_buffer.get<osmium::Relation>(0);
        pgsql_process_relation(rel);
    }
}

void output_pgsql_t::sync()
{
    for (auto const &t : m_tables) {
        t->sync();
    }
}

void output_pgsql_t::stop()
{
    for (auto &t : m_tables) {
        t->task_set(thread_pool().submit([&]() {
            t->stop(get_options()->slim && !get_options()->droptemp,
                    get_options()->enable_hstore_index,
                    get_options()->tblsmain_index);
        }));
    }

    if (get_options()->expire_tiles_zoom_min > 0) {
        m_expire.output_and_destroy(
            get_options()->expire_tiles_filename.c_str(),
            get_options()->expire_tiles_zoom_min);
    }
}

void output_pgsql_t::wait()
{
    for (auto &t : m_tables) {
        t->task_wait();
    }
}

void output_pgsql_t::node_add(osmium::Node const &node)
{
    taglist_t outtags;
    if (m_tagtransform->filter_tags(node, nullptr, nullptr, &outtags)) {
        return;
    }

    auto const geom = geom::transform(geom::create_point(node), *m_proj);
    m_expire.from_geometry(geom, node.id());
    auto const wkb = geom_to_ewkb(geom);
    m_tables[t_point]->write_row(node.id(), outtags, wkb);
}

void output_pgsql_t::way_add(osmium::Way *way)
{
    bool polygon = false;
    bool roads = false;
    taglist_t outtags;

    /* Check whether the way is: (1) Exportable, (2) Maybe a polygon */
    auto filter = m_tagtransform->filter_tags(*way, &polygon, &roads, &outtags);

    if (!filter) {
        /* Get actual node data and generate output */
        auto nnodes = middle().nodes_get_list(&(way->nodes()));
        if (nnodes > 1) {
            pgsql_out_way(*way, &outtags, polygon, roads);
        }
    }
}

// The roles of all available member ways of a relation are available in the
// Lua "filter_tags_relation_member" callback function. This function extracts
// the roles from all ways in the buffer and returns the list.
static rolelist_t get_rolelist(osmium::Relation const &rel,
                               osmium::memory::Buffer const &buffer)
{
    rolelist_t roles;

    auto it = buffer.select<osmium::Way>().cbegin();
    auto const end = buffer.select<osmium::Way>().cend();

    if (it == end) {
        return roles;
    }

    for (auto const &member : rel.members()) {
        if (member.type() == osmium::item_type::way &&
            member.ref() == it->id()) {
            roles.emplace_back(member.role());
            ++it;
            if (it == end) {
                break;
            }
        }
    }

    return roles;
}

/* This is the workhorse of pgsql_add_relation, split out because it is used as the callback for iterate relations */
void output_pgsql_t::pgsql_process_relation(osmium::Relation const &rel)
{
    taglist_t prefiltered_tags;
    if (m_tagtransform->filter_tags(rel, nullptr, nullptr, &prefiltered_tags)) {
        return;
    }

    m_buffer.clear();
    auto const num_ways =
        middle().rel_members_get(rel, &m_buffer, osmium::osm_entity_bits::way);

    if (num_ways == 0) {
        return;
    }

    bool roads = false;
    bool make_polygon = false;
    bool make_boundary = false;
    taglist_t outtags;

    rolelist_t xrole;
    if (!get_options()->tag_transform_script.empty()) {
        xrole = get_rolelist(rel, m_buffer);
    }

    // If it's a route relation make_boundary and make_polygon will be false
    // otherwise one or the other will be true.
    if (m_tagtransform->filter_rel_member_tags(
            prefiltered_tags, m_buffer, xrole, &make_boundary, &make_polygon,
            &roads, &outtags)) {
        return;
    }

    for (auto &w : m_buffer.select<osmium::Way>()) {
        middle().nodes_get_list(&(w.nodes()));
    }

    // linear features and boundaries
    // Needs to be done before the polygon treatment below because
    // for boundaries the way_area tag may be added.
    if (!make_polygon) {
        double const split_at =
            get_options()->projection->target_latlon() ? 1 : 100 * 1000;
        auto geom = geom::line_merge(geom::create_multilinestring(m_buffer));
        auto projected_geom = geom::transform(geom, *m_proj);
        if (!projected_geom.is_null() && split_at > 0.0) {
            projected_geom = geom::segmentize(projected_geom, split_at);
        }
        auto const geoms = geom::split_multi(projected_geom);
        for (auto const &sgeom : geoms) {
            m_expire.from_geometry(sgeom, -rel.id());
            auto const wkb = geom_to_ewkb(sgeom);
            m_tables[t_line]->write_row(-rel.id(), outtags, wkb);
            if (roads) {
                m_tables[t_roads]->write_row(-rel.id(), outtags, wkb);
            }
        }
    }

    // multipolygons and boundaries
    if (make_boundary || make_polygon) {
        auto const geoms =
            geom::split_multi(geom::create_multipolygon(rel, m_buffer),
                              !get_options()->enable_multi);
        for (auto const &sgeom : geoms) {
            auto const projected_geom = geom::transform(sgeom, *m_proj);
            m_expire.from_geometry(projected_geom, -rel.id());
            auto const wkb = geom_to_ewkb(projected_geom);
            if (m_enable_way_area) {
                double const area = calculate_area(
                    get_options()->reproject_area, sgeom, projected_geom);
                util::double_to_buffer tmp{area};
                outtags.set("way_area", tmp.c_str());
            }
            m_tables[t_poly]->write_row(-rel.id(), outtags, wkb);
        }
    }
}

void output_pgsql_t::relation_add(osmium::Relation const &rel)
{
    char const *const type = rel.tags()["type"];

    /* Must have a type field or we ignore it */
    if (!type) {
        return;
    }

    /* Only a limited subset of type= is supported, ignore other */
    if (std::strcmp(type, "route") != 0 &&
        std::strcmp(type, "multipolygon") != 0 &&
        std::strcmp(type, "boundary") != 0) {
        return;
    }

    pgsql_process_relation(rel);
}

/* Delete is easy, just remove all traces of this object. We don't need to
 * worry about finding objects that depend on it, since the same diff must
 * contain the change for that also. */
void output_pgsql_t::node_delete(osmid_t osm_id)
{
    if (m_expire.from_result(m_tables[t_point]->get_wkb(osm_id), osm_id) != 0) {
        m_tables[t_point]->delete_row(osm_id);
    }
}

/* Seperated out because we use it elsewhere */
void output_pgsql_t::pgsql_delete_way_from_output(osmid_t osm_id)
{
    /* Optimisation: we only need this is slim mode */
    if (!get_options()->slim) {
        return;
    }
    /* in droptemp mode we don't have indices and this takes ages. */
    if (get_options()->droptemp) {
        return;
    }

    m_tables[t_roads]->delete_row(osm_id);
    if (m_expire.from_result(m_tables[t_line]->get_wkb(osm_id), osm_id) != 0) {
        m_tables[t_line]->delete_row(osm_id);
    }
    if (m_expire.from_result(m_tables[t_poly]->get_wkb(osm_id), osm_id) != 0) {
        m_tables[t_poly]->delete_row(osm_id);
    }
}

void output_pgsql_t::way_delete(osmid_t osm_id)
{
    pgsql_delete_way_from_output(osm_id);
}

/* Relations are identified by using negative IDs */
void output_pgsql_t::pgsql_delete_relation_from_output(osmid_t osm_id)
{
    m_tables[t_roads]->delete_row(-osm_id);
    if (m_expire.from_result(m_tables[t_line]->get_wkb(-osm_id), -osm_id) !=
        0) {
        m_tables[t_line]->delete_row(-osm_id);
    }
    if (m_expire.from_result(m_tables[t_poly]->get_wkb(-osm_id), -osm_id) !=
        0) {
        m_tables[t_poly]->delete_row(-osm_id);
    }
}

void output_pgsql_t::relation_delete(osmid_t osm_id)
{
    pgsql_delete_relation_from_output(osm_id);
}

/* Modify is slightly trickier. The basic idea is we simply delete the
 * object and create it with the new parameters. Then we need to mark the
 * objects that depend on this one */
void output_pgsql_t::node_modify(osmium::Node const &node)
{
    node_delete(node.id());
    node_add(node);
}

void output_pgsql_t::way_modify(osmium::Way *way)
{
    way_delete(way->id());
    way_add(way);
}

void output_pgsql_t::relation_modify(osmium::Relation const &rel)
{
    relation_delete(rel.id());
    relation_add(rel);
}

void output_pgsql_t::start()
{
    for (auto &t : m_tables) {
        //setup the table in postgres
        t->start(get_options()->database_options.conninfo(),
                 get_options()->tblsmain_data);
    }
}

std::shared_ptr<output_t> output_pgsql_t::clone(
    std::shared_ptr<middle_query_t> const &mid,
    std::shared_ptr<db_copy_thread_t> const &copy_thread) const
{
    return std::shared_ptr<output_t>(
        new output_pgsql_t{this, mid, copy_thread});
}

output_pgsql_t::output_pgsql_t(
    std::shared_ptr<middle_query_t> const &mid,
    std::shared_ptr<thread_pool_t> thread_pool, options_t const &o,
    std::shared_ptr<db_copy_thread_t> const &copy_thread)
: output_t(mid, std::move(thread_pool), o), m_proj(o.projection),
  m_expire(o.expire_tiles_zoom, o.expire_tiles_max_bbox, o.projection),
  m_buffer(32768, osmium::memory::Buffer::auto_grow::yes),
  m_rels_buffer(1024, osmium::memory::Buffer::auto_grow::yes)
{
    log_debug("Using projection SRS {} ({})", o.projection->target_srs(),
              o.projection->target_desc());

    export_list exlist;

    m_enable_way_area = read_style_file(get_options()->style, &exlist);

    m_tagtransform = tagtransform_t::make_tagtransform(get_options(), exlist);

    //for each table
    for (size_t i = 0; i < t_MAX; ++i) {

        //figure out the columns this table needs
        columns_t columns = exlist.normal_columns(
            (i == t_point) ? osmium::item_type::node : osmium::item_type::way);

        //figure out what name we are using for this and what type
        std::string name = get_options()->prefix;
        std::string type;
        switch (i) {
        case t_point:
            name += "_point";
            type = "POINT";
            break;
        case t_line:
            name += "_line";
            type = "LINESTRING";
            break;
        case t_poly:
            name += "_polygon";
            type =
                "GEOMETRY"; // Actually POLGYON & MULTIPOLYGON but no way to limit to just these two
            break;
        case t_roads:
            name += "_roads";
            type = "LINESTRING";
            break;
        default:
            std::abort(); // should never be here
        }

        m_tables[i] = std::make_unique<table_t>(
            name, type, columns, get_options()->hstore_columns,
            get_options()->projection->target_srs(), get_options()->append,
            get_options()->hstore_mode, copy_thread,
            get_options()->output_dbschema);
    }
}

output_pgsql_t::output_pgsql_t(
    output_pgsql_t const *other, std::shared_ptr<middle_query_t> const &mid,
    std::shared_ptr<db_copy_thread_t> const &copy_thread)
: output_t(mid, other->m_thread_pool, *other->get_options()),
  m_tagtransform(other->m_tagtransform->clone()),
  m_enable_way_area(other->m_enable_way_area),
  m_proj(get_options()->projection),
  m_expire(get_options()->expire_tiles_zoom,
           get_options()->expire_tiles_max_bbox, get_options()->projection),
  m_buffer(1024, osmium::memory::Buffer::auto_grow::yes),
  m_rels_buffer(1024, osmium::memory::Buffer::auto_grow::yes)
{
    for (size_t i = 0; i < t_MAX; ++i) {
        //copy constructor will just connect to the already there table
        m_tables[i] =
            std::make_unique<table_t>(*(other->m_tables[i].get()), copy_thread);
    }
}

output_pgsql_t::~output_pgsql_t() = default;

void output_pgsql_t::merge_expire_trees(output_t *other)
{
    auto *const opgsql = dynamic_cast<output_pgsql_t *>(other);
    if (opgsql) {
        m_expire.merge_and_destroy(&opgsql->m_expire);
    }
}
