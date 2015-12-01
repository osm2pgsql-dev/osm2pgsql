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

/* make the diagnostic information work with older versions of
 * boost - the function signature changed at version 1.54.
 */
#if BOOST_VERSION >= 105400
#define BOOST_DIAGNOSTIC_INFO(e) boost::diagnostic_information((e), true)
#else
#define BOOST_DIAGNOSTIC_INFO(e) boost::diagnostic_information((e))
#endif

#define SRID (reproj->project_getprojinfo()->srs)

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

int output_pgsql_t::pgsql_out_node(osmid_t id, const taglist_t &tags, double node_lat, double node_lon)
{
    taglist_t outtags;
    if (m_tagtransform->filter_node_tags(tags, *m_export_list.get(), outtags))
        return 1;

    expire->from_bbox(node_lon, node_lat, node_lon, node_lat);
    m_tables[t_point]->write_node(id, outtags, node_lat, node_lon);

    return 0;
}


/*
COPY planet_osm (osm_id, name, place, landuse, leisure, "natural", man_made, waterway, highway, railway, amenity, tourism, learning, bu
ilding, bridge, layer, way) FROM stdin;
198497  Bedford Road    \N      \N      \N      \N      \N      \N      residential     \N      \N      \N      \N      \N      \N    \N       0102000020E610000004000000452BF702B342D5BF1C60E63BF8DF49406B9C4D470037D5BF5471E316F3DF4940DFA815A6EF35D5BF9AE95E27F5DF4940B41EB
E4C1421D5BF24D06053E7DF4940
212696  Oswald Road     \N      \N      \N      \N      \N      \N      minor   \N      \N      \N      \N      \N      \N      \N    0102000020E610000004000000467D923B6C22D5BFA359D93EE4DF4940B3976DA7AD11D5BF84BBB376DBDF4940997FF44D9A06D5BF4223D8B8FEDF49404D158C4AEA04D
5BF5BB39597FCDF4940
*/
int output_pgsql_t::pgsql_out_way(osmid_t id, const taglist_t &tags, const nodelist_t &nodes, int exists)
{
    int polygon = 0, roads = 0;
    double split_at;

    /* If the flag says this object may exist already, delete it first */
    if (exists) {
        pgsql_delete_way_from_output(id);
        // TODO: this now only has an effect when called from the iterate_ways
        // call-back, so we need some alternative way to trigger this within
        // osmdata_t.
        const idlist_t rel_ids = m_mid->relations_using_way(id);
        for (idlist_t::const_iterator itr = rel_ids.begin();
             itr != rel_ids.end(); ++itr) {
            rels_pending_tracker->mark(*itr);
        }
    }

    taglist_t outtags;
    if (m_tagtransform->filter_way_tags(tags, &polygon, &roads, *m_export_list.get(),
                                        outtags))
        return 0;
    /* Split long ways after around 1 degree or 100km */
    if (m_options.projection->get_proj_id() == PROJ_LATLONG)
        split_at = 1;
    else
        split_at = 100 * 1000;

    tag_t *areatag = 0;
    auto wkbs = builder.get_wkb_split(nodes, polygon, split_at);
    for (const auto& wkb: wkbs) {
        /* FIXME: there should be a better way to detect polygons */
        if (wkb.is_polygon()) {
            expire->from_nodes_poly(nodes, id);
            if ((wkb.area > 0.0) && m_enable_way_area) {
                char tmp[32];
                snprintf(tmp, sizeof(tmp), "%g", wkb.area);
                if (!areatag) {
                    outtags.push_dedupe(tag_t("way_area", tmp));
                    areatag = outtags.find("way_area");
                } else
                    areatag->value = tmp;
            }
            m_tables[t_poly]->write_row(id, outtags, wkb.geom);
        } else {
            expire->from_nodes_line(nodes);
            m_tables[t_line]->write_row(id, outtags, wkb.geom);
            if (roads)
                m_tables[t_roads]->write_row(id, outtags, wkb.geom);
        }
    }

    return 0;
}

int output_pgsql_t::pgsql_out_relation(osmid_t id, const taglist_t &rel_tags,
                           const multinodelist_t &xnodes, const multitaglist_t & xtags,
                           const idlist_t &xid, const rolelist_t &xrole,
                           bool pending)
{
    if (xnodes.empty())
        return 0;

    int roads = 0;
    int make_polygon = 0;
    int make_boundary = 0;
    double split_at;

    std::vector<int> members_superseeded(xnodes.size(), 0);
    taglist_t outtags;

    //if its a route relation make_boundary and make_polygon will be false otherwise one or the other will be true
    if (m_tagtransform->filter_rel_member_tags(rel_tags, xtags, xrole,
              &(members_superseeded[0]), &make_boundary, &make_polygon, &roads,
              *m_export_list.get(), outtags)) {
        return 0;
    }

    /* Split long linear ways after around 1 degree or 100km (polygons not effected) */
    if (m_options.projection->get_proj_id() == PROJ_LATLONG)
        split_at = 1;
    else
        split_at = 100 * 1000;

    //this will either make lines or polygons (unless the lines arent a ring or are less than 3 pts) depending on the tag transform above
    //TODO: pick one or the other based on which we expect to care about
    auto wkbs  = builder.build_both(xnodes, make_polygon, m_options.enable_multi, split_at, id);

    if (wkbs.empty()) {
        return 0;
    }

    tag_t *areatag = 0;
    char tmp[32];
    for (const auto& wkb: wkbs) {
        expire->from_wkb(wkb.geom.c_str(), -id);
        /* FIXME: there should be a better way to detect polygons */
        if (wkb.is_polygon()) {
            if ((wkb.area > 0.0) && m_enable_way_area) {
                snprintf(tmp, sizeof(tmp), "%g", wkb.area);
                if (!areatag) {
                    outtags.push_dedupe(tag_t("way_area", tmp));
                    areatag = outtags.find("way_area");
                }
            }
            m_tables[t_poly]->write_row(-id, outtags, wkb.geom);
        } else {
            m_tables[t_line]->write_row(-id, outtags, wkb.geom);
            if (roads)
                m_tables[t_roads]->write_row(-id, outtags, wkb.geom);
        }
    }

    /* Tagtransform will have marked those member ways of the relation that
     * have fully been dealt with as part of the multi-polygon entry.
     * Set them in the database as done and delete their entry to not
     * have duplicates */
    //dont do this when working with pending relations as its not needed
    if (make_polygon) {
        for (size_t i=0; i < xid.size(); i++) {
            if (members_superseeded[i]) {
                pgsql_delete_way_from_output(xid[i]);
                if(!pending)
                    ways_done_tracker->mark(xid[i]);
            }
        }
    }

    // If the tag transform said the polygon looked like a boundary we want to make that as well
    // If we are making a boundary then also try adding any relations which form complete rings
    // The linear variants will have already been processed above
    if (make_boundary) {
        wkbs = builder.build_polygons(xnodes, m_options.enable_multi, id);
        for (const auto& wkb: wkbs) {
            expire->from_wkb(wkb.geom.c_str(), -id);
            if ((wkb.area > 0.0) && m_enable_way_area) {
                snprintf(tmp, sizeof(tmp), "%g", wkb.area);
                if (!areatag) {
                    outtags.push_dedupe(tag_t("way_area", tmp));
                    areatag = outtags.find("way_area");
                }
            }
            m_tables[t_poly]->write_row(-id, outtags, wkb.geom);
        }
    }

    return 0;
}


void output_pgsql_t::enqueue_ways(pending_queue_t &job_queue, osmid_t id, size_t output_id, size_t& added) {
    osmid_t const prev = ways_pending_tracker->last_returned();
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
    osmid_t popped = ways_pending_tracker->pop_mark();
    if(!id_tracker::is_valid(popped))
        return;

    //get all the ones up to the id that was passed in
    while (popped < id) {
        if (!ways_done_tracker->is_marked(popped)) {
            job_queue.push(pending_job_t(popped, output_id));
            added++;
        }
        popped = ways_pending_tracker->pop_mark();
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
    taglist_t tags_int;
    nodelist_t nodes_int;
    int ret = 0;

    // Try to fetch the way from the DB
    if (m_mid->ways_get(id, tags_int, nodes_int)) {
        // Output the way
        //ret = reprocess_way(id, nodes_int, count_int, &tags_int, exists);
        ret = pgsql_out_way(id, tags_int, nodes_int, exists);
    }

    return ret;
}

void output_pgsql_t::enqueue_relations(pending_queue_t &job_queue, osmid_t id, size_t output_id, size_t& added) {
    osmid_t const prev = rels_pending_tracker->last_returned();
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
    osmid_t popped = rels_pending_tracker->pop_mark();
    if(!id_tracker::is_valid(popped))
        return;

    //get all the ones up to the id that was passed in
    while (popped < id) {
        job_queue.push(pending_job_t(popped, output_id));
        added++;
        popped = rels_pending_tracker->pop_mark();
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
    taglist_t tags_int;
    memberlist_t members_int;
    int ret = 0;

    // Try to fetch the relation from the DB
    if (m_mid->relations_get(id, members_int, tags_int)) {
        ret = pgsql_process_relation(id, members_int, tags_int, exists, true);
    }

    return ret;
}

void output_pgsql_t::commit()
{
    for (const auto &t : m_tables) {
        t->commit();
    }
}

void output_pgsql_t::stop()
{
    if (m_options.parallel_indexing) {
      std::vector<std::future<void>> outs;
      outs.reserve(m_tables.size());

      for (auto &t : m_tables) {
          outs.push_back(std::async(std::launch::async, &table_t::stop, t));
      }

      // XXX If one of the stop functions throws an error, this collects all
      //     the other threads first before exiting. That might take a very
      //     long time if large indexes are created.
      for (auto &f : outs) {
        f.get();
      }

    } else {
      for (const auto &t : m_tables) {
        t->stop();
      }
    }

    expire->output_and_destroy();
    expire.reset();
}

int output_pgsql_t::node_add(osmid_t id, double lat, double lon, const taglist_t &tags)
{
  pgsql_out_node(id, tags, lat, lon);

  return 0;
}

int output_pgsql_t::way_add(osmid_t id, const idlist_t &nds, const taglist_t &tags)
{
  int polygon = 0;
  int roads = 0;
  taglist_t outtags;

  /* Check whether the way is: (1) Exportable, (2) Maybe a polygon */
  int filter = m_tagtransform->filter_way_tags(tags, &polygon, &roads, *m_export_list.get(), outtags);

  /* If this isn't a polygon then it can not be part of a multipolygon
     Hence only polygons are "pending" */
  if (!filter && polygon) { ways_pending_tracker->mark(id); }

  if( !polygon && !filter )
  {
    /* Get actual node data and generate output */
    nodelist_t nodes;
    m_mid->nodes_get_list(nodes, nds);
    pgsql_out_way(id, outtags, nodes, 0);
  }
  return 0;
}


/* This is the workhorse of pgsql_add_relation, split out because it is used as the callback for iterate relations */
int output_pgsql_t::pgsql_process_relation(osmid_t id, const memberlist_t &members,
                                           const taglist_t &tags, int exists, bool pending)
{
  /* If the flag says this object may exist already, delete it first */
  if(exists)
      pgsql_delete_relation_from_output(id);

  taglist_t outtags;

  if (m_tagtransform->filter_rel_tags(tags, *m_export_list.get(), outtags))
      return 1;

  idlist_t xid2;
  multitaglist_t xtags2;
  multinodelist_t xnodes;

  for (memberlist_t::const_iterator it = members.begin(); it != members.end(); ++it)
  {
    /* Need to handle more than just ways... */
    if (it->type == OSMTYPE_WAY)
        xid2.push_back(it->id);
  }

  idlist_t xid;
  m_mid->ways_get_list(xid2, xid, xtags2, xnodes);
  int polygon = 0, roads = 0;
  multitaglist_t xtags(xid.size(), taglist_t());
  rolelist_t xrole(xid.size(), 0);

  for (size_t i = 0; i < xid.size(); i++) {
      for (size_t j = i; j < members.size(); j++) {
          if (members[j].id == xid[i]) {
              //filter the tags on this member because we got it from the middle
              //and since the middle is no longer tied to the output it no longer
              //shares any kind of tag transform and therefore all original tags
              //will come back and need to be filtered by individual outputs before
              //using these ways
              m_tagtransform->filter_way_tags(xtags2[i], &polygon, &roads,
                                              *m_export_list.get(), xtags[i]);
              //TODO: if the filter says that this member is now not interesting we
              //should decrement the count and remove his nodes and tags etc. for
              //now we'll just keep him with no tags so he will get filtered later
              xrole[i] = &members[j].role;
              break;
          }
      }
  }

  /* At some point we might want to consider storing the retrieved data in the members, rather than as separate arrays */
  pgsql_out_relation(id, outtags, xnodes, xtags, xid, xrole, pending);

  return 0;
}

int output_pgsql_t::relation_add(osmid_t id, const memberlist_t &members, const taglist_t &tags)
{
  const std::string *type = tags.get("type");

  /* Must have a type field or we ignore it */
  if (!type)
      return 0;

  /* Only a limited subset of type= is supported, ignore other */
  if ( (*type != "route") && (*type != "multipolygon") && (*type != "boundary"))
    return 0;


  return pgsql_process_relation(id, members, tags, 0);
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

    if ( expire->from_db(m_tables[t_point].get(), osm_id) != 0)
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
    if ( expire->from_db(m_tables[t_line].get(), osm_id) != 0)
        m_tables[t_line]->delete_row(osm_id);
    if ( expire->from_db(m_tables[t_poly].get(), osm_id) != 0)
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
    if ( expire->from_db(m_tables[t_line].get(), -osm_id) != 0)
        m_tables[t_line]->delete_row(-osm_id);
    if ( expire->from_db(m_tables[t_poly].get(), -osm_id) != 0)
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
int output_pgsql_t::node_modify(osmid_t osm_id, double lat, double lon, const taglist_t &tags)
{
    if( !m_options.slim )
    {
        fprintf( stderr, "Cannot apply diffs unless in slim mode\n" );
        util::exit_nicely();
    }
    node_delete(osm_id);
    node_add(osm_id, lat, lon, tags);
    return 0;
}

int output_pgsql_t::way_modify(osmid_t osm_id, const idlist_t &nodes, const taglist_t &tags)
{
    if( !m_options.slim )
    {
        fprintf( stderr, "Cannot apply diffs unless in slim mode\n" );
        util::exit_nicely();
    }
    way_delete(osm_id);
    way_add(osm_id, nodes, tags);

    return 0;
}

int output_pgsql_t::relation_modify(osmid_t osm_id, const memberlist_t &members, const taglist_t &tags)
{
    if( !m_options.slim )
    {
        fprintf( stderr, "Cannot apply diffs unless in slim mode\n" );
        util::exit_nicely();
    }
    relation_delete(osm_id);
    relation_add(osm_id, members, tags);
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

std::shared_ptr<output_t> output_pgsql_t::clone(const middle_query_t* cloned_middle) const {
    output_pgsql_t *clone = new output_pgsql_t(*this);
    clone->m_mid = cloned_middle;
    //NOTE: we need to know which ways were used by relations so each thread
    //must have a copy of the original marked done ways, its read only so its ok
    clone->ways_done_tracker = ways_done_tracker;
    return std::shared_ptr<output_t>(clone);
}

output_pgsql_t::output_pgsql_t(const middle_query_t* mid_, const options_t &options_)
    : output_t(mid_, options_),
      ways_pending_tracker(new id_tracker()),
      ways_done_tracker(new id_tracker()),
      rels_pending_tracker(new id_tracker()) {

    reproj = m_options.projection;
    builder.set_exclude_broken_polygon(m_options.excludepoly);
    if (m_options.reproject_area) builder.set_reprojection(reproj.get());

    m_export_list.reset(new export_list());

    m_enable_way_area = read_style_file( m_options.style, m_export_list.get() );

    try {
        m_tagtransform.reset(new tagtransform(&m_options));
    }
    catch(const std::runtime_error& e) {
        fprintf(stderr, "%s\n", e.what());
        fprintf(stderr, "Error: Failed to initialise tag processing.\n");
        util::exit_nicely();
    }

    expire.reset(new expire_tiles(&m_options));

    //for each table
    m_tables.reserve(t_MAX);
    for (int i = 0; i < t_MAX; i++) {

        //figure out the columns this table needs
        columns_t columns = m_export_list->normal_columns((i == t_point)?OSMTYPE_NODE:OSMTYPE_WAY);

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
        m_tables.push_back(std::shared_ptr<table_t>(
            new table_t(
                m_options.database_options.conninfo(), name, type, columns, m_options.hstore_columns, SRID,
                m_options.append, m_options.slim, m_options.droptemp, m_options.hstore_mode,
                m_options.enable_hstore_index, m_options.tblsmain_data, m_options.tblsmain_index
            )
        ));
    }
}

output_pgsql_t::output_pgsql_t(const output_pgsql_t& other):
    output_t(other.m_mid, other.m_options), m_tagtransform(new tagtransform(&m_options)), m_enable_way_area(other.m_enable_way_area),
    m_export_list(new export_list(*other.m_export_list)), reproj(other.reproj),
    expire(new expire_tiles(&m_options)),
    ways_pending_tracker(new id_tracker()), ways_done_tracker(new id_tracker()), rels_pending_tracker(new id_tracker())
{
    builder.set_exclude_broken_polygon(m_options.excludepoly);
    if (m_options.reproject_area) builder.set_reprojection(reproj.get());
    for(std::vector<std::shared_ptr<table_t> >::const_iterator t = other.m_tables.begin(); t != other.m_tables.end(); ++t) {
        //copy constructor will just connect to the already there table
        m_tables.push_back(std::shared_ptr<table_t>(new table_t(**t)));
    }
}

output_pgsql_t::~output_pgsql_t() {
}

size_t output_pgsql_t::pending_count() const {
    return ways_pending_tracker->size() + rels_pending_tracker->size();
}

void output_pgsql_t::merge_pending_relations(std::shared_ptr<output_t> other) {
    std::shared_ptr<id_tracker> tracker = other->get_pending_relations();
    osmid_t id;
    while(tracker.get() && id_tracker::is_valid((id = tracker->pop_mark()))){
        rels_pending_tracker->mark(id);
    }
}
void output_pgsql_t::merge_expire_trees(std::shared_ptr<output_t> other) {
    if(other->get_expire_tree().get())
        expire->merge_and_destroy(*other->get_expire_tree());
}

std::shared_ptr<id_tracker> output_pgsql_t::get_pending_relations() {
    return rels_pending_tracker;
}
std::shared_ptr<expire_tiles> output_pgsql_t::get_expire_tree() {
    return expire;
}
