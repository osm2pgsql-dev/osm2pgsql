/* Implements the mid-layer processing for osm2pgsql
 * using several PostgreSQL tables
 * 
 * This layer stores data read in from the planet.osm file
 * and is then read by the backend processing code to
 * emit the final geometry-enabled output formats
*/

#include "config.h"

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <time.h>
#include <stdexcept>

#ifdef HAVE_PTHREAD
#include <pthread.h>
#endif

#include "osmtypes.hpp"
#include "reprojection.hpp"
#include "output-pgsql.hpp"
#include "options.hpp"
#include "middle.hpp"
#include "pgsql.hpp"
#include "expire-tiles.hpp"
#include "wildcmp.hpp"
#include "node-ram-cache.hpp"
#include "taginfo_impl.hpp"
#include "tagtransform.hpp"
#include "buffer.hpp"
#include "util.hpp"

#include <boost/bind.hpp>
#include <boost/format.hpp>
#include <boost/foreach.hpp>
#include <boost/make_shared.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <iostream>
#include <limits>
#include <stdexcept>

#define SRID (reproj->project_getprojinfo()->srs)

/* FIXME: Shouldn't malloc this all to begin with but call realloc()
   as required. The program will most likely segfault if it reads a
   style file with more styles than this */
#define MAX_STYLES 1000

#define NUM_TABLES (output_pgsql_t::t_MAX)

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

int output_pgsql_t::pgsql_out_node(osmid_t id, struct keyval *tags, double node_lat, double node_lon)
{

    int filter = m_tagtransform->filter_node_tags(tags, m_export_list.get());
    int i;
    struct keyval *tag;

    if (filter) return 1;

    expire->from_bbox(node_lon, node_lat, node_lon, node_lat);
    m_tables[t_point]->write_node(id, tags, node_lat, node_lon);

    return 0;
}

/*static int tag_indicates_polygon(enum OsmType type, const char *key)
{
    int i;

    if (!strcmp(key, "area"))
        return 1;

    for (i=0; i < exportListCount[type]; i++) {
        if( strcmp( exportList[type][i].name, key ) == 0 )
            return exportList[type][i].flags & FLAG_POLYGON;
    }

    return 0;
}*/

/*
COPY planet_osm (osm_id, name, place, landuse, leisure, "natural", man_made, waterway, highway, railway, amenity, tourism, learning, bu
ilding, bridge, layer, way) FROM stdin;
198497  Bedford Road    \N      \N      \N      \N      \N      \N      residential     \N      \N      \N      \N      \N      \N    \N       0102000020E610000004000000452BF702B342D5BF1C60E63BF8DF49406B9C4D470037D5BF5471E316F3DF4940DFA815A6EF35D5BF9AE95E27F5DF4940B41EB
E4C1421D5BF24D06053E7DF4940
212696  Oswald Road     \N      \N      \N      \N      \N      \N      minor   \N      \N      \N      \N      \N      \N      \N    0102000020E610000004000000467D923B6C22D5BFA359D93EE4DF4940B3976DA7AD11D5BF84BBB376DBDF4940997FF44D9A06D5BF4223D8B8FEDF49404D158C4AEA04D
5BF5BB39597FCDF4940
*/
int output_pgsql_t::pgsql_out_way(osmid_t id, struct keyval *tags, const struct osmNode *nodes, int count, int exists)
{
    int polygon = 0, roads = 0;
    int i, wkt_size;
    double split_at;

    /* If the flag says this object may exist already, delete it first */
    if(exists) {
        pgsql_delete_way_from_output(id);
        // TODO: this now only has an effect when called from the iterate_ways
        // call-back, so we need some alternative way to trigger this within
        // osmdata_t.
        const std::vector<osmid_t> rel_ids = m_mid->relations_using_way(id);
        for (std::vector<osmid_t>::const_iterator itr = rel_ids.begin();
             itr != rel_ids.end(); ++itr) {
            rels_pending_tracker->mark(*itr);
        }
    }

    if (m_tagtransform->filter_way_tags(tags, &polygon, &roads, m_export_list.get()))
        return 0;
    /* Split long ways after around 1 degree or 100km */
    if (m_options.projection->get_proj_id() == PROJ_LATLONG)
        split_at = 1;
    else
        split_at = 100 * 1000;

    geometry_builder::maybe_wkts_t wkts = builder.get_wkt_split(nodes, count, polygon, split_at);
    for(geometry_builder::wkt_itr wkt = wkts->begin(); wkt != wkts->end(); ++wkt)
    {
        /* FIXME: there should be a better way to detect polygons */
        if (boost::starts_with(wkt->geom, "POLYGON") || boost::starts_with(wkt->geom, "MULTIPOLYGON")) {
            expire->from_nodes_poly(nodes, count, id);
            if ((wkt->area > 0.0) && m_enable_way_area) {
                char tmp[32];
                snprintf(tmp, sizeof(tmp), "%g", wkt->area);
                keyval::addItem(tags, "way_area", tmp, 0);
            }
            m_tables[t_poly]->write_wkt(id, tags, wkt->geom.c_str());
        } else {
            expire->from_nodes_line(nodes, count);
            m_tables[t_line]->write_wkt(id, tags, wkt->geom.c_str());
            if (roads)
                m_tables[t_roads]->write_wkt(id, tags, wkt->geom.c_str());
        }
    }
	
    return 0;
}

int output_pgsql_t::pgsql_out_relation(osmid_t id, struct keyval *rel_tags, int member_count, const struct osmNode * const *xnodes, struct keyval *xtags, const int *xcount, const osmid_t *xid, const char * const *xrole)
{
    if (member_count == 0)
        return 0;

    int i, wkt_size;
    int roads = 0;
    int make_polygon = 0;
    int make_boundary = 0;
    int * members_superseeded;
    double split_at;

    members_superseeded = (int *)calloc(sizeof(int), member_count);

    //if its a route relation make_boundary and make_polygon will be false otherwise one or the other will be true
    if (m_tagtransform->filter_rel_member_tags(rel_tags, member_count, xtags, xrole, members_superseeded, &make_boundary, &make_polygon, &roads, m_export_list.get())) {
        free(members_superseeded);
        return 0;
    }
    
    /* Split long linear ways after around 1 degree or 100km (polygons not effected) */
    if (m_options.projection->get_proj_id() == PROJ_LATLONG)
        split_at = 1;
    else
        split_at = 100 * 1000;

    //this will either make lines or polygons (unless the lines arent a ring or are less than 3 pts) depending on the tag transform above
    //TODO: pick one or the other based on which we expect to care about
    geometry_builder::maybe_wkts_t wkts  = builder.build_both(xnodes, xcount, make_polygon, m_options.enable_multi, split_at, id);

    if (!wkts->size()) {
        free(members_superseeded);
        return 0;
    }

    for(geometry_builder::wkt_itr wkt = wkts->begin(); wkt != wkts->end(); ++wkt)
    {
        expire->from_wkt(wkt->geom.c_str(), -id);
        /* FIXME: there should be a better way to detect polygons */
        if (boost::starts_with(wkt->geom, "POLYGON") || boost::starts_with(wkt->geom, "MULTIPOLYGON")) {
            if ((wkt->area > 0.0) && m_enable_way_area) {
                char tmp[32];
                snprintf(tmp, sizeof(tmp), "%g", wkt->area);
                keyval::addItem(rel_tags, "way_area", tmp, 0);
            }
            m_tables[t_poly]->write_wkt(-id, rel_tags, wkt->geom.c_str());
        } else {
            m_tables[t_line]->write_wkt(-id, rel_tags, wkt->geom.c_str());
            if (roads)
                m_tables[t_roads]->write_wkt(-id, rel_tags, wkt->geom.c_str());
        }
    }

    /* Tagtransform will have marked those member ways of the relation that
     * have fully been dealt with as part of the multi-polygon entry.
     * Set them in the database as done and delete their entry to not
     * have duplicates */
    if (make_polygon) {
        for (i=0; xcount[i]; i++) {
            if (members_superseeded[i]) {
                ways_done_tracker->mark(xid[i]);
                pgsql_delete_way_from_output(xid[i]);
            }
        }
    }

    free(members_superseeded);

    // If the tag transform said the polygon looked like a boundary we want to make that as well
    // If we are making a boundary then also try adding any relations which form complete rings
    // The linear variants will have already been processed above
    if (make_boundary) {
        wkts = builder.build_polygons(xnodes, xcount, m_options.enable_multi, id);
        for(geometry_builder::wkt_itr wkt = wkts->begin(); wkt != wkts->end(); ++wkt)
        {
            expire->from_wkt(wkt->geom.c_str(), -id);
            if ((wkt->area > 0.0) && m_enable_way_area) {
                char tmp[32];
                snprintf(tmp, sizeof(tmp), "%g", wkt->area);
                keyval::addItem(rel_tags, "way_area", tmp, 0);
            }
            m_tables[t_poly]->write_wkt(-id, rel_tags, wkt->geom.c_str());
        }
    }

    return 0;
}



namespace {
/* Using pthreads requires us to shoe-horn everything into various void*
 * pointers. Improvement for the future: just use boost::thread. */
struct pthread_thunk {
    table_t *ptr;
};

extern "C" void *pthread_output_pgsql_stop_one(void *arg) {
    pthread_thunk *thunk = static_cast<pthread_thunk *>(arg);
    thunk->ptr->stop();
    return NULL;
};
} // anonymous namespace

void output_pgsql_t::enqueue_ways(pending_queue_t &job_queue, osmid_t id, size_t output_id, size_t& added) {
    int ret = 0;

    //make sure we get the one passed in
    if (!ways_done_tracker->is_marked(id)) {
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
    if(popped == id) {
        popped = ways_pending_tracker->pop_mark();
    }
    if (!ways_done_tracker->is_marked(popped)) {
        job_queue.push(pending_job_t(popped, output_id));
        added++;
    }
}

int output_pgsql_t::pending_way(osmid_t id, int exists) {
    keyval tags_int;
    osmNode *nodes_int;
    int count_int;
    int ret = 0;

    keyval::initList(&tags_int);
    // Try to fetch the way from the DB
    if (!m_mid->ways_get(id, &tags_int, &nodes_int, &count_int)) {
        // Output the way
        //ret = reprocess_way(id, nodes_int, count_int, &tags_int, exists);
        ret = pgsql_out_way(id, &tags_int, nodes_int, count_int, exists);
        free(nodes_int);
    }
    keyval::resetList(&tags_int);

    return ret;
}

void output_pgsql_t::enqueue_relations(pending_queue_t &job_queue, osmid_t id, size_t output_id, size_t& added) {
    int ret = 0;

    //make sure we get the one passed in
    job_queue.push(pending_job_t(id, output_id));
    added++;

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
    if(popped == id) {
        popped = rels_pending_tracker->pop_mark();
    }
    job_queue.push(pending_job_t(popped, output_id));
    added++;
}

int output_pgsql_t::pending_relation(osmid_t id, int exists) {
    keyval tags_int;
    member *members_int;
    int count_int;
    int ret = 0;

    keyval::initList(&tags_int);
    // Try to fetch the relation from the DB
    if (!m_mid->relations_get(id, &members_int, &count_int, &tags_int)) {
        ret = pgsql_process_relation(id, members_int, count_int, &tags_int, exists);
        free(members_int);
    }
    keyval::resetList(&tags_int);

    return ret;
}

void output_pgsql_t::commit()
{
    for (int i=0; i<NUM_TABLES; i++) {
        m_tables[i]->commit();
    }
}

void output_pgsql_t::stop()
{
    int i;
#ifdef HAVE_PTHREAD
    pthread_t threads[NUM_TABLES];
#endif
  
#ifdef HAVE_PTHREAD
    if (m_options.parallel_indexing) {
      pthread_thunk thunks[NUM_TABLES];
      for (i=0; i<NUM_TABLES; i++) {
          thunks[i].ptr = m_tables[i].get();
      }

      for (i=0; i<NUM_TABLES; i++) {
          int ret = pthread_create(&threads[i], NULL, pthread_output_pgsql_stop_one, &thunks[i]);
          if (ret) {
              fprintf(stderr, "pthread_create() returned an error (%d)", ret);
              util::exit_nicely();
          }
      }
  
      for (i=0; i<NUM_TABLES; i++) {
          int ret = pthread_join(threads[i], NULL);
          if (ret) {
              fprintf(stderr, "pthread_join() returned an error (%d)", ret);
              util::exit_nicely();
          }
      }
    } else {
#endif

    /* No longer need to access middle layer -- release memory */
    //TODO: just let the destructor do this
    for (i=0; i<NUM_TABLES; i++)
        m_tables[i]->stop();

#ifdef HAVE_PTHREAD
    }
#endif

    expire->output_and_destroy();
    expire.reset();
}

int output_pgsql_t::node_add(osmid_t id, double lat, double lon, struct keyval *tags)
{
  pgsql_out_node(id, tags, lat, lon);

  return 0;
}

int output_pgsql_t::way_add(osmid_t id, osmid_t *nds, int nd_count, struct keyval *tags)
{
  int polygon = 0;
  int roads = 0;


  /* Check whether the way is: (1) Exportable, (2) Maybe a polygon */
  int filter = m_tagtransform->filter_way_tags(tags, &polygon, &roads, m_export_list.get());

  /* If this isn't a polygon then it can not be part of a multipolygon
     Hence only polygons are "pending" */
  if (!filter && polygon) { ways_pending_tracker->mark(id); }

  if( !polygon && !filter )
  {
    /* Get actual node data and generate output */
    struct osmNode *nodes = (struct osmNode *)malloc( sizeof(struct osmNode) * nd_count );
    int count = m_mid->nodes_get_list( nodes, nds, nd_count );
    pgsql_out_way(id, tags, nodes, count, 0);
    free(nodes);
  }
  return 0;
}


/* This is the workhorse of pgsql_add_relation, split out because it is used as the callback for iterate relations */
int output_pgsql_t::pgsql_process_relation(osmid_t id, const struct member *members, int member_count, struct keyval *tags, int exists)
{
    int i, j, count, count2;

  /* If the flag says this object may exist already, delete it first */
  if(exists)
      pgsql_delete_relation_from_output(id);

  if (m_tagtransform->filter_rel_tags(tags, m_export_list.get())) {
      return 1;
  }

  osmid_t *xid2 = (osmid_t *)malloc( (member_count+1) * sizeof(osmid_t) );
  const char **xrole = (const char **)malloc( (member_count+1) * sizeof(const char *) );
  int *xcount = (int *)malloc( (member_count+1) * sizeof(int) );
  keyval *xtags  = new keyval[member_count+1];
  struct osmNode **xnodes = (struct osmNode **)malloc( (member_count+1) * sizeof(struct osmNode*) );

  count = 0;
  for( i=0; i<member_count; i++ )
  {
    /* Need to handle more than just ways... */
    if( members[i].type != OSMTYPE_WAY )
        continue;
    xid2[count] = members[i].id;
    count++;
  }

  osmid_t *xid = (osmid_t *)malloc( sizeof(osmid_t) * (count + 1));
  count2 = m_mid->ways_get_list(xid2, count, xid, xtags, xnodes, xcount);
  int polygon = 0, roads = 0;;

  for (i = 0; i < count2; i++) {
      for (j = i; j < member_count; j++) {
          if (members[j].id == xid[i]) {
              //filter the tags on this member because we got it from the middle
              //and since the middle is no longer tied to the output it no longer
              //shares any kind of tag transform and therefore all original tags
              //will come back and need to be filtered by individual outputs before
              //using these ways
              m_tagtransform->filter_way_tags(&xtags[i], &polygon, &roads, m_export_list.get());
              //TODO: if the filter says that this member is now not interesting we
              //should decrement the count and remove his nodes and tags etc. for
              //now we'll just keep him with no tags so he will get filtered later
              break;
          }
      }
      xrole[i] = members[j].role;
  }
  xnodes[count2] = NULL;
  xcount[count2] = 0;
  xid[count2] = 0;
  xrole[count2] = NULL;

  /* At some point we might want to consider storing the retrieved data in the members, rather than as separate arrays */
  pgsql_out_relation(id, tags, count2, xnodes, xtags, xcount, xid, xrole);

  for( i=0; i<count2; i++ )
  {
    keyval::resetList( &(xtags[i]) );
    free( xnodes[i] );
  }

  free(xid2);
  free(xid);
  free(xrole);
  free(xcount);
  delete [] xtags;
  free(xnodes);
  return 0;
}

int output_pgsql_t::relation_add(osmid_t id, struct member *members, int member_count, struct keyval *tags)
{
  const char *type = keyval::getItem(tags, "type");

  /* Must have a type field or we ignore it */
  if (!type)
      return 0;

  /* Only a limited subset of type= is supported, ignore other */
  if ( (strcmp(type, "route") != 0) && (strcmp(type, "multipolygon") != 0) && (strcmp(type, "boundary") != 0))
    return 0;


  return pgsql_process_relation(id, members, member_count, tags, 0);
}
#define UNUSED  __attribute__ ((unused))

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
int output_pgsql_t::node_modify(osmid_t osm_id, double lat, double lon, struct keyval *tags)
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

int output_pgsql_t::way_modify(osmid_t osm_id, osmid_t *nodes, int node_count, struct keyval *tags)
{
    if( !m_options.slim )
    {
        fprintf( stderr, "Cannot apply diffs unless in slim mode\n" );
        util::exit_nicely();
    }
    way_delete(osm_id);
    way_add(osm_id, nodes, node_count, tags);

    return 0;
}

int output_pgsql_t::relation_modify(osmid_t osm_id, struct member *members, int member_count, struct keyval *tags)
{
    if( !m_options.slim )
    {
        fprintf( stderr, "Cannot apply diffs unless in slim mode\n" );
        util::exit_nicely();
    }
    relation_delete(osm_id);
    relation_add(osm_id, members, member_count, tags);
    return 0;
}

int output_pgsql_t::start()
{
    for(std::vector<boost::shared_ptr<table_t> >::iterator table = m_tables.begin(); table != m_tables.end(); ++table)
    {
        //setup the table in postgres
        table->get()->start();
    }

    return 0;
}

boost::shared_ptr<output_t> output_pgsql_t::clone(const middle_query_t* cloned_middle) const {
    output_pgsql_t *clone = new output_pgsql_t(*this);
    clone->m_mid = cloned_middle;
    return boost::shared_ptr<output_t>(clone);
}

output_pgsql_t::output_pgsql_t(const middle_query_t* mid_, const options_t &options_)
    : output_t(mid_, options_),
      ways_pending_tracker(new id_tracker()),
      ways_done_tracker(new id_tracker()),
      rels_pending_tracker(new id_tracker()) {

    reproj = m_options.projection;
    builder.set_exclude_broken_polygon(m_options.excludepoly);

    m_export_list.reset(new export_list());

    m_enable_way_area = read_style_file( m_options.style, m_export_list.get() );

    try {
        m_tagtransform.reset(new tagtransform(&m_options));
    }
    catch(std::runtime_error& e) {
        fprintf(stderr, "%s\n", e.what());
        fprintf(stderr, "Error: Failed to initialise tag processing.\n");
        util::exit_nicely();
    }

    expire.reset(new expire_tiles(&m_options));

    //for each table
    m_tables.reserve(NUM_TABLES);
    for (int i=0; i<NUM_TABLES; i++) {

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
        m_tables.push_back(boost::shared_ptr<table_t>(
            new table_t(
                m_options.conninfo, name, type, columns, m_options.hstore_columns, SRID, m_options.scale,
                m_options.append, m_options.slim, m_options.droptemp, m_options.hstore_mode,
                m_options.enable_hstore_index, m_options.tblsmain_data, m_options.tblsmain_index
            )
        ));
    }
}

output_pgsql_t::output_pgsql_t(const output_pgsql_t& other):
    output_t(other.m_mid, other.m_options), m_tagtransform(new tagtransform(&m_options)), m_enable_way_area(other.m_enable_way_area),
    m_export_list(new export_list(*other.m_export_list)), reproj(other.reproj),
    ways_pending_tracker(new id_tracker()), ways_done_tracker(new id_tracker()), rels_pending_tracker(new id_tracker()),
    expire(new expire_tiles(&m_options))
{
    builder.set_exclude_broken_polygon(m_options.excludepoly);
    for(std::vector<boost::shared_ptr<table_t> >::const_iterator t = other.m_tables.begin(); t != other.m_tables.end(); ++t) {
        //copy constructor will just connect to the already there table
        m_tables.push_back(boost::shared_ptr<table_t>(new table_t(**t)));
    }
}

output_pgsql_t::~output_pgsql_t() {
}

size_t output_pgsql_t::pending_count() const {
    return ways_pending_tracker->size() + rels_pending_tracker->size();
}

void output_pgsql_t::merge_pending_relations(boost::shared_ptr<output_t> other) {
    boost::shared_ptr<id_tracker> tracker = other->get_pending_relations();
    osmid_t id;
    while(tracker.get() && id_tracker::is_valid((id = tracker->pop_mark()))){
        rels_pending_tracker->mark(id);
    }
}
void output_pgsql_t::merge_expire_trees(boost::shared_ptr<output_t> other) {
    if(other->get_expire_tree().get())
        expire->merge_and_destroy(*other->get_expire_tree());
}

boost::shared_ptr<id_tracker> output_pgsql_t::get_pending_relations() {
    return rels_pending_tracker;
}
boost::shared_ptr<expire_tiles> output_pgsql_t::get_expire_tree() {
    return expire;
}
