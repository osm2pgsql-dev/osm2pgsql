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
#include <ctime>
#include <unistd.h>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/bind.hpp>
#include <boost/exception_ptr.hpp>
#include <boost/format.hpp>

#include "expire-tiles.hpp"
#include "middle.hpp"
#include "node-ram-cache.hpp"
#include "options.hpp"
#include "osmtypes.hpp"
#include "output-pgsql.hpp"
#include "pgsql.hpp"
#include "reprojection.hpp"
#include "taginfo_impl.hpp"
#include "tagtransform.hpp"
#include "util.hpp"
#include "wildcmp.hpp"
#include "wkb.hpp"

/* make the diagnostic information work with older versions of
 * boost - the function signature changed at version 1.54.
 */
#if BOOST_VERSION >= 105400
#define BOOST_DIAGNOSTIC_INFO(e) boost::diagnostic_information((e), true)
#else
#define BOOST_DIAGNOSTIC_INFO(e) boost::diagnostic_information((e))
#endif

/* example from: pg_dump -F p -t planet_osm gis
COPY planet_osm (osm_id, name, place, landuse, leisure, "natural", man_made, waterway, highway, railway, amenity, tourism, learning, building, bridge, layer, way) FROM stdin;
17959841        \N      \N      \N      \N      \N      \N      \N      bus_stop        \N      \N      \N      \N      \N      \N    -\N      0101000020E610000030CCA462B6C3D4BF92998C9B38E04940
17401934        The Horn        \N      \N      \N      \N      \N      \N      \N      \N      pub     \N      \N      \N      \N    -\N      0101000020E6100000C12FC937140FD5BFB4D2F4FB0CE04940
...

mine - 01 01000000 48424298424242424242424256427364
psql - 01 01000020 E6100000 30CCA462B6C3D4BF92998C9B38E04940
       01 01000020 E6100000 48424298424242424242424256427364
0x2000_0000 = hasSRID, following 4 bytes = srid, not supported by geos WKBWriter
Workaround - output SRID=4326;<WKB>
*/


/*
COPY planet_osm (osm_id, name, place, landuse, leisure, "natural", man_made, waterway, highway, railway, amenity, tourism, learning, bu
ilding, bridge, layer, way) FROM stdin;
198497  Bedford Road    \N      \N      \N      \N      \N      \N      residential     \N      \N      \N      \N      \N      \N    \N       0102000020E610000004000000452BF702B342D5BF1C60E63BF8DF49406B9C4D470037D5BF5471E316F3DF4940DFA815A6EF35D5BF9AE95E27F5DF4940B41EB
E4C1421D5BF24D06053E7DF4940
212696  Oswald Road     \N      \N      \N      \N      \N      \N      minor   \N      \N      \N      \N      \N      \N      \N    0102000020E610000004000000467D923B6C22D5BFA359D93EE4DF4940B3976DA7AD11D5BF84BBB376DBDF4940997FF44D9A06D5BF4223D8B8FEDF49404D158C4AEA04D
5BF5BB39597FCDF4940
*/
void output_pgsql_t::pgsql_out_way(osmium::Way const &way, taglist_t *tags,
                                   bool polygon, bool roads)
{
    if (polygon && way.is_closed()) {
        auto wkb = m_builder.get_wkb_polygon(way);
        if (!wkb.empty()) {
            expire.from_wkb(wkb.c_str(), way.id());
            if (m_enable_way_area) {
                char tmp[32];
                auto const area =
                    m_options.reproject_area
                        ? ewkb::parser_t(wkb).get_area<reprojection>(
                              m_options.projection.get())
                        : ewkb::parser_t(wkb)
                              .get_area<osmium::geom::IdentityProjection>();
                snprintf(tmp, sizeof(tmp), "%g", area);
                tags->push_override(tag_t("way_area", tmp));
            }
            m_tables[t_poly]->write_row(way.id(), *tags, wkb);
        }
    } else {
        double const split_at = m_options.projection->target_latlon() ? 1 : 100 * 1000;
        for (auto const &wkb : m_builder.get_wkb_line(way.nodes(), split_at)) {
            expire.from_wkb(wkb.c_str(), way.id());
            m_tables[t_line]->write_row(way.id(), *tags, wkb);
            if (roads) {
                m_tables[t_roads]->write_row(way.id(), *tags, wkb);
            }
        }

    }
}

void output_pgsql_t::enqueue_ways(pending_queue_t &job_queue, osmid_t id, size_t output_id, size_t& added) {
    osmid_t const prev = ways_pending_tracker.last_returned();
    if (id_tracker::is_valid(prev) && prev >= id) {
        if (prev > id) {
            job_queue.push(pending_job_t(id, output_id));
        }
        // already done the job
        return;
    }

    //make sure we get the one passed in
    if(!ways_done_tracker->is_marked(id) && id_tracker::is_valid(id)) {
        job_queue.push(pending_job_t(id, output_id));
        added++;
    }

    //grab the first one or bail if its not valid
    osmid_t popped = ways_pending_tracker.pop_mark();
    if(!id_tracker::is_valid(popped))
        return;

    //get all the ones up to the id that was passed in
    while (popped < id) {
        if (!ways_done_tracker->is_marked(popped)) {
            job_queue.push(pending_job_t(popped, output_id));
            added++;
        }
        popped = ways_pending_tracker.pop_mark();
    }

    //make sure to get this one as well and move to the next
    if(popped > id) {
        if (!ways_done_tracker->is_marked(popped) && id_tracker::is_valid(popped)) {
            job_queue.push(pending_job_t(popped, output_id));
            added++;
        }
    }
}

int output_pgsql_t::pending_way(osmid_t id, int exists) {
    // Try to fetch the way from the DB
    buffer.clear();
    if (m_mid->ways_get(id, buffer)) {
        /* If the flag says this object may exist already, delete it first */
        if (exists) {
            pgsql_delete_way_from_output(id);
            // TODO: this now only has an effect when called from the iterate_ways
            // call-back, so we need some alternative way to trigger this within
            // osmdata_t.
            const idlist_t rel_ids = m_mid->relations_using_way(id);
            for (auto &mid: rel_ids) {
                rels_pending_tracker.mark(mid);
            }
        }

        taglist_t outtags;
        int polygon;
        int roads;
        auto &way = buffer.get<osmium::Way>(0);
        if (!m_tagtransform->filter_tags(way, &polygon, &roads,
                                         *m_export_list.get(), outtags)) {
            auto nnodes = m_mid->nodes_get_list(&(way.nodes()));
            if (nnodes > 1) {
                pgsql_out_way(way, &outtags, polygon, roads);
                return 1;
            }
        }
    }

    return 0;
}

void output_pgsql_t::enqueue_relations(pending_queue_t &job_queue, osmid_t id, size_t output_id, size_t& added) {
    osmid_t const prev = rels_pending_tracker.last_returned();
    if (id_tracker::is_valid(prev) && prev >= id) {
        if (prev > id) {
            job_queue.push(pending_job_t(id, output_id));
        }
        // already done the job
        return;
    }

    //make sure we get the one passed in
    if(id_tracker::is_valid(id)) {
        job_queue.push(pending_job_t(id, output_id));
        added++;
    }

    //grab the first one or bail if its not valid
    osmid_t popped = rels_pending_tracker.pop_mark();
    if(!id_tracker::is_valid(popped))
        return;

    //get all the ones up to the id that was passed in
    while (popped < id) {
        job_queue.push(pending_job_t(popped, output_id));
        added++;
        popped = rels_pending_tracker.pop_mark();
    }

    //make sure to get this one as well and move to the next
    if(popped > id) {
        if(id_tracker::is_valid(popped)) {
            job_queue.push(pending_job_t(popped, output_id));
            added++;
        }
    }
}

int output_pgsql_t::pending_relation(osmid_t id, int exists) {
    // Try to fetch the relation from the DB
    // Note that we cannot use the global buffer here because
    // we cannot keep a reference to the relation and an autogrow buffer
    // might be relocated when more data is added.
    rels_buffer.clear();
    if (m_mid->relations_get(id, rels_buffer)) {
        // If the flag says this object may exist already, delete it first.
        if (exists) {
            pgsql_delete_relation_from_output(id);
        }

        auto const &rel = rels_buffer.get<osmium::Relation>(0);
        return pgsql_process_relation(rel, true);
    }

    return 0;
}

void output_pgsql_t::commit()
{
    for (const auto &t : m_tables) {
        t->commit();
    }
}

void output_pgsql_t::stop(osmium::thread::Pool *pool)
{
    // attempt to stop tables in parallel
    for (auto &t : m_tables) {
        pool->submit(std::bind(&table_t::stop, t));
    }

    if (m_options.expire_tiles_zoom_min > 0) {
        expire.output_and_destroy(m_options.expire_tiles_filename.c_str(),
                                  m_options.expire_tiles_zoom_min);
    }
}

int output_pgsql_t::node_add(osmium::Node const &node)
{
    taglist_t outtags;
    if (m_tagtransform->filter_tags(node, nullptr, nullptr,
                                    *m_export_list.get(), outtags))
        return 1;

    auto wkb = m_builder.get_wkb_node(node.location());
    expire.from_wkb(wkb.c_str(), node.id());
    m_tables[t_point]->write_row(node.id(), outtags, wkb);

    return 0;
}

int output_pgsql_t::way_add(osmium::Way *way)
{
    int polygon = 0;
    int roads = 0;
    taglist_t outtags;

    /* Check whether the way is: (1) Exportable, (2) Maybe a polygon */
    auto filter = m_tagtransform->filter_tags(*way, &polygon, &roads,
                                              *m_export_list.get(), outtags);

    /* If this isn't a polygon then it can not be part of a multipolygon
       Hence only polygons are "pending" */
    if (!filter && polygon) { ways_pending_tracker.mark(way->id()); }

    if( !polygon && !filter )
    {
        /* Get actual node data and generate output */
        auto nnodes = m_mid->nodes_get_list(&(way->nodes()));
        if (nnodes > 1) {
            pgsql_out_way(*way, &outtags, polygon, roads);
        }
    }
    return 0;
}


/* This is the workhorse of pgsql_add_relation, split out because it is used as the callback for iterate relations */
int output_pgsql_t::pgsql_process_relation(osmium::Relation const &rel,
                                           bool pending)
{
    taglist_t prefiltered_tags;
    if (m_tagtransform->filter_tags(rel, nullptr, nullptr, *m_export_list.get(),
                                    prefiltered_tags)) {
        return 1;
    }

    idlist_t xid2;
    for (auto const &m : rel.members()) {
        /* Need to handle more than just ways... */
        if (m.type() == osmium::item_type::way) {
            xid2.push_back(m.ref());
        }
    }

    buffer.clear();
    rolelist_t xrole;
    auto num_ways = m_mid->rel_way_members_get(rel, &xrole, buffer);

    if (num_ways == 0)
        return 0;

  int roads = 0;
  int make_polygon = 0;
  int make_boundary = 0;
  std::vector<int> members_superseded(num_ways, 0);
  taglist_t outtags;

  // If it's a route relation make_boundary and make_polygon will be false
  // otherwise one or the other will be true.
  if (m_tagtransform->filter_rel_member_tags(
          prefiltered_tags, buffer, xrole, &(members_superseded[0]),
          &make_boundary, &make_polygon, &roads, *m_export_list.get(),
          outtags)) {
      return 0;
  }

  for (auto &w : buffer.select<osmium::Way>()) {
      m_mid->nodes_get_list(&(w.nodes()));
  }

  // linear features and boundaries
  // Needs to be done before the polygon treatment below because
  // for boundaries the way_area tag may be added.
  if (!make_polygon) {
      double const split_at = m_options.projection->target_latlon() ? 1 : 100 * 1000;
      auto wkbs = m_builder.get_wkb_multiline(buffer, split_at);
      for (auto const &wkb : wkbs) {
          expire.from_wkb(wkb.c_str(), -rel.id());
          m_tables[t_line]->write_row(-rel.id(), outtags, wkb);
          if (roads)
              m_tables[t_roads]->write_row(-rel.id(), outtags, wkb);
      }
  }

  // multipolygons and boundaries
  if (make_boundary || make_polygon) {
      auto wkbs = m_builder.get_wkb_multipolygon(rel, buffer);

      char tmp[32];
      for (auto const &wkb : wkbs) {
          expire.from_wkb(wkb.c_str(), -rel.id());
          if (m_enable_way_area) {
              auto const area =
                  m_options.reproject_area
                      ? ewkb::parser_t(wkb).get_area<reprojection>(
                            m_options.projection.get())
                      : ewkb::parser_t(wkb)
                            .get_area<osmium::geom::IdentityProjection>();
              snprintf(tmp, sizeof(tmp), "%g", area);
              outtags.push_override(tag_t("way_area", tmp));
          }
          m_tables[t_poly]->write_row(-rel.id(), outtags, wkb);
      }

      /* Tagtransform will have marked those member ways of the relation that
         * have fully been dealt with as part of the multi-polygon entry.
         * Set them in the database as done and delete their entry to not
         * have duplicates */
      if (make_polygon) {
          size_t j = 0;
          for (auto &w : buffer.select<osmium::Way>()) {
              if (members_superseded[j]) {
                  pgsql_delete_way_from_output(w.id());
                  // When working with pending relations this is not needed.
                  if (!pending) {
                      ways_done_tracker->mark(w.id());
                  }
              }
              ++j;
          }
      }
  }

  return 0;
}

int output_pgsql_t::relation_add(osmium::Relation const &rel)
{
    char const *type = rel.tags()["type"];

    /* Must have a type field or we ignore it */
    if (!type)
        return 0;

    /* Only a limited subset of type= is supported, ignore other */
    if (strcmp(type, "route") != 0 && strcmp(type, "multipolygon") != 0
        && strcmp(type, "boundary") != 0) {
        return 0;
    }

    return pgsql_process_relation(rel, false);
}

/* Delete is easy, just remove all traces of this object. We don't need to
 * worry about finding objects that depend on it, since the same diff must
 * contain the change for that also. */
int output_pgsql_t::node_delete(osmid_t osm_id)
{
    if( !m_options.slim )
    {
        fprintf( stderr, "Cannot apply diffs unless in slim mode\n" );
        util::exit_nicely();
    }

    if ( expire.from_db(m_tables[t_point].get(), osm_id) != 0)
        m_tables[t_point]->delete_row(osm_id);

    return 0;
}

/* Seperated out because we use it elsewhere */
int output_pgsql_t::pgsql_delete_way_from_output(osmid_t osm_id)
{
    /* Optimisation: we only need this is slim mode */
    if( !m_options.slim )
        return 0;
    /* in droptemp mode we don't have indices and this takes ages. */
    if (m_options.droptemp)
        return 0;

    m_tables[t_roads]->delete_row(osm_id);
    if ( expire.from_db(m_tables[t_line].get(), osm_id) != 0)
        m_tables[t_line]->delete_row(osm_id);
    if ( expire.from_db(m_tables[t_poly].get(), osm_id) != 0)
        m_tables[t_poly]->delete_row(osm_id);
    return 0;
}

int output_pgsql_t::way_delete(osmid_t osm_id)
{
    if( !m_options.slim )
    {
        fprintf( stderr, "Cannot apply diffs unless in slim mode\n" );
        util::exit_nicely();
    }
    pgsql_delete_way_from_output(osm_id);
    return 0;
}

/* Relations are identified by using negative IDs */
int output_pgsql_t::pgsql_delete_relation_from_output(osmid_t osm_id)
{
    m_tables[t_roads]->delete_row(-osm_id);
    if ( expire.from_db(m_tables[t_line].get(), -osm_id) != 0)
        m_tables[t_line]->delete_row(-osm_id);
    if ( expire.from_db(m_tables[t_poly].get(), -osm_id) != 0)
        m_tables[t_poly]->delete_row(-osm_id);
    return 0;
}

int output_pgsql_t::relation_delete(osmid_t osm_id)
{
    if( !m_options.slim )
    {
        fprintf( stderr, "Cannot apply diffs unless in slim mode\n" );
        util::exit_nicely();
    }
    pgsql_delete_relation_from_output(osm_id);
    return 0;
}

/* Modify is slightly trickier. The basic idea is we simply delete the
 * object and create it with the new parameters. Then we need to mark the
 * objects that depend on this one */
int output_pgsql_t::node_modify(osmium::Node const &node)
{
    if (!m_options.slim) {
        fprintf(stderr, "Cannot apply diffs unless in slim mode\n");
        util::exit_nicely();
    }
    node_delete(node.id());
    node_add(node);
    return 0;
}

int output_pgsql_t::way_modify(osmium::Way *way)
{
    if( !m_options.slim )
    {
        fprintf( stderr, "Cannot apply diffs unless in slim mode\n" );
        util::exit_nicely();
    }
    way_delete(way->id());
    way_add(way);

    return 0;
}

int output_pgsql_t::relation_modify(osmium::Relation const &rel)
{
    if( !m_options.slim )
    {
        fprintf( stderr, "Cannot apply diffs unless in slim mode\n" );
        util::exit_nicely();
    }
    relation_delete(rel.id());
    relation_add(rel);
    return 0;
}

int output_pgsql_t::start()
{
    for(std::vector<std::shared_ptr<table_t> >::iterator table = m_tables.begin(); table != m_tables.end(); ++table)
    {
        //setup the table in postgres
        table->get()->start();
    }

    return 0;
}

std::shared_ptr<output_t> output_pgsql_t::clone(const middle_query_t* cloned_middle) const
{
    auto *clone = new output_pgsql_t(*this);
    clone->m_mid = cloned_middle;
    return std::shared_ptr<output_t>(clone);
}

output_pgsql_t::output_pgsql_t(const middle_query_t *mid, const options_t &o)
: output_t(mid, o), m_builder(o.projection, o.enable_multi),
  expire(o.expire_tiles_zoom, o.expire_tiles_max_bbox, o.projection),
  ways_done_tracker(new id_tracker()),
  buffer(32768, osmium::memory::Buffer::auto_grow::yes),
  rels_buffer(1024, osmium::memory::Buffer::auto_grow::yes)
{
    m_export_list.reset(new export_list());

    m_enable_way_area = read_style_file( m_options.style, m_export_list.get() );

    try {
        m_tagtransform = tagtransform_t::make_tagtransform(&m_options);
    }
    catch(const std::runtime_error& e) {
        fprintf(stderr, "%s\n", e.what());
        fprintf(stderr, "Error: Failed to initialise tag processing.\n");
        util::exit_nicely();
    }

    //for each table
    m_tables.reserve(t_MAX);
    for (int i = 0; i < t_MAX; i++) {

        //figure out the columns this table needs
        columns_t columns = m_export_list->normal_columns((i == t_point)
                            ? osmium::item_type::node
                            : osmium::item_type::way);

        //figure out what name we are using for this and what type
        std::string name = m_options.prefix;
        std::string type;
        switch(i)
        {
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
                type = "GEOMETRY"; // Actually POLGYON & MULTIPOLYGON but no way to limit to just these two
                break;
            case t_roads:
                name += "_roads";
                type = "LINESTRING";
                break;
            default:
                //TODO: error message about coding error
                util::exit_nicely();
        }

        //tremble in awe of this massive constructor! seriously we are trying to avoid passing an
        //options object because we want to make use of the table_t in output_mutli_t which could
        //have a different tablespace/hstores/etc per table
        m_tables.push_back(std::shared_ptr<table_t>(new table_t(
            m_options.database_options.conninfo(), name, type, columns,
            m_options.hstore_columns, m_options.projection->target_srs(),
            m_options.append, m_options.slim, m_options.droptemp,
            m_options.hstore_mode, m_options.enable_hstore_index,
            m_options.tblsmain_data, m_options.tblsmain_index)));
    }
}

output_pgsql_t::output_pgsql_t(const output_pgsql_t &other)
: output_t(other.m_mid, other.m_options),
  m_tagtransform(tagtransform_t::make_tagtransform(&m_options)),
  m_enable_way_area(other.m_enable_way_area),
  m_export_list(new export_list(*other.m_export_list)),
  m_builder(m_options.projection, other.m_options.enable_multi),
  expire(m_options.expire_tiles_zoom, m_options.expire_tiles_max_bbox,
         m_options.projection),
  //NOTE: we need to know which ways were used by relations so each thread
  //must have a copy of the original marked done ways, its read only so its ok
  ways_done_tracker(other.ways_done_tracker),
  buffer(1024, osmium::memory::Buffer::auto_grow::yes),
  rels_buffer(1024, osmium::memory::Buffer::auto_grow::yes)
{
    for(std::vector<std::shared_ptr<table_t> >::const_iterator t = other.m_tables.begin(); t != other.m_tables.end(); ++t) {
        //copy constructor will just connect to the already there table
        m_tables.push_back(std::shared_ptr<table_t>(new table_t(**t)));
    }
}

output_pgsql_t::~output_pgsql_t() {
}

size_t output_pgsql_t::pending_count() const {
    return ways_pending_tracker.size() + rels_pending_tracker.size();
}

void output_pgsql_t::merge_pending_relations(output_t *other)
{
    auto opgsql = dynamic_cast<output_pgsql_t *>(other);
    if (opgsql) {
        osmid_t id;
        while (id_tracker::is_valid((id = opgsql->rels_pending_tracker.pop_mark()))) {
            rels_pending_tracker.mark(id);
        }
    }
}
void output_pgsql_t::merge_expire_trees(output_t *other)
{
    auto *opgsql = dynamic_cast<output_pgsql_t *>(other);
    if (opgsql)
        expire.merge_and_destroy(opgsql->expire);
}

