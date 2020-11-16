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
#include <unistd.h>

#include "expire-tiles.hpp"
#include "logging.hpp"
#include "middle.hpp"
#include "node-ram-cache.hpp"
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

void output_pgsql_t::pgsql_out_way(osmium::Way const &way, taglist_t *tags,
                                   bool polygon, bool roads)
{
    if (polygon && way.is_closed()) {
        auto wkb = m_builder.get_wkb_polygon(way);
        if (!wkb.empty()) {
            expire.from_wkb(wkb.c_str(), way.id());
            if (m_enable_way_area) {
                auto const area =
                    m_options.reproject_area
                        ? ewkb::parser_t(wkb).get_area<reprojection>(
                              m_options.projection.get())
                        : ewkb::parser_t(wkb)
                              .get_area<osmium::geom::IdentityProjection>();
                util::double_to_buffer tmp{area};
                tags->set("way_area", tmp.c_str());
            }
            m_tables[t_poly]->write_row(way.id(), *tags, wkb);
        }
    } else {
        double const split_at =
            m_options.projection->target_latlon() ? 1 : 100 * 1000;
        for (auto const &wkb : m_builder.get_wkb_line(way.nodes(), split_at)) {
            expire.from_wkb(wkb.c_str(), way.id());
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
    buffer.clear();
    if (m_mid->way_get(id, buffer)) {
        pgsql_delete_way_from_output(id);

        taglist_t outtags;
        int polygon;
        int roads;
        auto &way = buffer.get<osmium::Way>(0);
        if (!m_tagtransform->filter_tags(way, &polygon, &roads, outtags)) {
            auto nnodes = m_mid->nodes_get_list(&(way.nodes()));
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
    rels_buffer.clear();
    if (m_mid->relation_get(id, rels_buffer)) {
        pgsql_delete_relation_from_output(id);

        auto const &rel = rels_buffer.get<osmium::Relation>(0);
        pgsql_process_relation(rel);
    }
}

void output_pgsql_t::sync()
{
    for (auto const &t : m_tables) {
        t->sync();
    }
}

void output_pgsql_t::stop(thread_pool_t *pool)
{
    // attempt to stop tables in parallel
    for (auto &t : m_tables) {
        pool->submit([&]() {
            t->stop(m_options.slim & !m_options.droptemp,
                    m_options.enable_hstore_index, m_options.tblsmain_index);
        });
    }

    if (m_options.expire_tiles_zoom_min > 0) {
        expire.output_and_destroy(m_options.expire_tiles_filename.c_str(),
                                  m_options.expire_tiles_zoom_min);
    }
}

void output_pgsql_t::node_add(osmium::Node const &node)
{
    taglist_t outtags;
    if (m_tagtransform->filter_tags(node, nullptr, nullptr, outtags)) {
        return;
    }

    auto wkb = m_builder.get_wkb_node(node.location());
    expire.from_wkb(wkb.c_str(), node.id());
    m_tables[t_point]->write_row(node.id(), outtags, wkb);
}

void output_pgsql_t::way_add(osmium::Way *way)
{
    int polygon = 0;
    int roads = 0;
    taglist_t outtags;

    /* Check whether the way is: (1) Exportable, (2) Maybe a polygon */
    auto filter = m_tagtransform->filter_tags(*way, &polygon, &roads, outtags);

    if (!filter) {
        /* Get actual node data and generate output */
        auto nnodes = m_mid->nodes_get_list(&(way->nodes()));
        if (nnodes > 1) {
            pgsql_out_way(*way, &outtags, polygon, roads);
        }
    }
}

/* This is the workhorse of pgsql_add_relation, split out because it is used as the callback for iterate relations */
void output_pgsql_t::pgsql_process_relation(osmium::Relation const &rel)
{
    taglist_t prefiltered_tags;
    if (m_tagtransform->filter_tags(rel, nullptr, nullptr, prefiltered_tags)) {
        return;
    }

    buffer.clear();
    rolelist_t xrole;
    auto num_ways = m_mid->rel_way_members_get(rel, &xrole, buffer);

    if (num_ways == 0) {
        return;
    }

    int roads = 0;
    int make_polygon = 0;
    int make_boundary = 0;
    taglist_t outtags;

    // If it's a route relation make_boundary and make_polygon will be false
    // otherwise one or the other will be true.
    if (m_tagtransform->filter_rel_member_tags(prefiltered_tags, buffer, xrole,
                                               &make_boundary, &make_polygon,
                                               &roads, outtags)) {
        return;
    }

    for (auto &w : buffer.select<osmium::Way>()) {
        m_mid->nodes_get_list(&(w.nodes()));
    }

    // linear features and boundaries
    // Needs to be done before the polygon treatment below because
    // for boundaries the way_area tag may be added.
    if (!make_polygon) {
        double const split_at =
            m_options.projection->target_latlon() ? 1 : 100 * 1000;
        auto wkbs = m_builder.get_wkb_multiline(buffer, split_at);
        for (auto const &wkb : wkbs) {
            expire.from_wkb(wkb.c_str(), -rel.id());
            m_tables[t_line]->write_row(-rel.id(), outtags, wkb);
            if (roads) {
                m_tables[t_roads]->write_row(-rel.id(), outtags, wkb);
            }
        }
    }

    // multipolygons and boundaries
    if (make_boundary || make_polygon) {
        auto wkbs = m_builder.get_wkb_multipolygon(rel, buffer, m_options.enable_multi);

        for (auto const &wkb : wkbs) {
            expire.from_wkb(wkb.c_str(), -rel.id());
            if (m_enable_way_area) {
                auto const area =
                    m_options.reproject_area
                        ? ewkb::parser_t(wkb).get_area<reprojection>(
                              m_options.projection.get())
                        : ewkb::parser_t(wkb)
                              .get_area<osmium::geom::IdentityProjection>();
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
    if (expire.from_db(m_tables[t_point].get(), osm_id) != 0) {
        m_tables[t_point]->delete_row(osm_id);
    }
}

/* Seperated out because we use it elsewhere */
void output_pgsql_t::pgsql_delete_way_from_output(osmid_t osm_id)
{
    /* Optimisation: we only need this is slim mode */
    if (!m_options.slim) {
        return;
    }
    /* in droptemp mode we don't have indices and this takes ages. */
    if (m_options.droptemp) {
        return;
    }

    m_tables[t_roads]->delete_row(osm_id);
    if (expire.from_db(m_tables[t_line].get(), osm_id) != 0) {
        m_tables[t_line]->delete_row(osm_id);
    }
    if (expire.from_db(m_tables[t_poly].get(), osm_id) != 0) {
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
    if (expire.from_db(m_tables[t_line].get(), -osm_id) != 0) {
        m_tables[t_line]->delete_row(-osm_id);
    }
    if (expire.from_db(m_tables[t_poly].get(), -osm_id) != 0) {
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
        t->start(m_options.database_options.conninfo(),
                 m_options.tblsmain_data);
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
    std::shared_ptr<middle_query_t> const &mid, options_t const &o,
    std::shared_ptr<db_copy_thread_t> const &copy_thread)
: output_t(mid, o), m_builder(o.projection),
  expire(o.expire_tiles_zoom, o.expire_tiles_max_bbox, o.projection),
  buffer(32768, osmium::memory::Buffer::auto_grow::yes),
  rels_buffer(1024, osmium::memory::Buffer::auto_grow::yes)
{
    log_info("Using projection SRS {} ({})", o.projection->target_srs(),
             o.projection->target_desc());

    export_list exlist;

    m_enable_way_area = read_style_file(m_options.style, &exlist);

    m_tagtransform = tagtransform_t::make_tagtransform(&m_options, exlist);

    //for each table
    for (size_t i = 0; i < t_MAX; ++i) {

        //figure out the columns this table needs
        columns_t columns = exlist.normal_columns(
            (i == t_point) ? osmium::item_type::node : osmium::item_type::way);

        //figure out what name we are using for this and what type
        std::string name = m_options.prefix;
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

        m_tables[i].reset(
            new table_t{name, type, columns, m_options.hstore_columns,
                        m_options.projection->target_srs(), m_options.append,
                        m_options.hstore_mode, copy_thread,
                        m_options.output_dbschema});
    }
}

output_pgsql_t::output_pgsql_t(
    output_pgsql_t const *other, std::shared_ptr<middle_query_t> const &mid,
    std::shared_ptr<db_copy_thread_t> const &copy_thread)
: output_t(mid, other->m_options),
  m_tagtransform(other->m_tagtransform->clone()),
  m_enable_way_area(other->m_enable_way_area),
  m_builder(m_options.projection),
  expire(m_options.expire_tiles_zoom, m_options.expire_tiles_max_bbox,
         m_options.projection),
  buffer(1024, osmium::memory::Buffer::auto_grow::yes),
  rels_buffer(1024, osmium::memory::Buffer::auto_grow::yes)
{
    for (size_t i = 0; i < t_MAX; ++i) {
        //copy constructor will just connect to the already there table
        m_tables[i].reset(
            new table_t{*(other->m_tables[i].get()), copy_thread});
    }
}

output_pgsql_t::~output_pgsql_t() = default;

void output_pgsql_t::merge_expire_trees(output_t *other)
{
    auto *const opgsql = dynamic_cast<output_pgsql_t *>(other);
    if (opgsql) {
        expire.merge_and_destroy(opgsql->expire);
    }
}
