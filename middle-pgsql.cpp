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
#include <math.h>
#include <time.h>
#include <errno.h>

#ifdef HAVE_PTHREAD
#include <pthread.h>
#endif

#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif

#ifdef HAVE_MMAP
#include <sys/mman.h>
#ifndef  MAP_ANONYMOUS
#ifdef MAP_ANON
#define MAP_ANONYMOUS MAP_ANON
#endif
#endif
#endif

#ifdef _WIN32
using namespace std;
#endif

#ifdef _MSC_VER
#define alloca _alloca
#endif

#include <libpq-fe.h>

#include "osmtypes.hpp"
#include "middle-pgsql.hpp"
#include "output-pgsql.hpp"
#include "options.hpp"
#include "node-ram-cache.hpp"
#include "node-persistent-cache.hpp"
#include "pgsql.hpp"
#include "util.hpp"

#include <stdexcept>
#include <boost/format.hpp>
#include <boost/unordered_map.hpp>

struct progress_info {
  time_t start;
  time_t end;
  int count;
  int finished;
};

enum table_id {
    t_node, t_way, t_rel
} ;

middle_pgsql_t::table_desc::table_desc(const char *name_,
                                       const char *start_,
                                       const char *create_,
                                       const char *create_index_,
                                       const char *prepare_,
                                       const char *prepare_intarray_,
                                       const char *copy_,
                                       const char *analyze_,
                                       const char *stop_,
                                       const char *array_indexes_)
    : name(name_),
      start(start_),
      create(create_),
      create_index(create_index_),
      prepare(prepare_),
      prepare_intarray(prepare_intarray_),
      copy(copy_),
      analyze(analyze_),
      stop(stop_),
      array_indexes(array_indexes_),
      copyMode(0),
      transactionMode(0),
      sql_conn(NULL)
{}

#define HELPER_STATE_UNINITIALIZED -1
#define HELPER_STATE_FORKED -2
#define HELPER_STATE_RUNNING 0
#define HELPER_STATE_FINISHED 1
#define HELPER_STATE_CONNECTED 2
#define HELPER_STATE_FAILED 3

namespace {
char *pgsql_store_nodes(const osmid_t *nds, const int& nd_count)
{
  static char *buffer;
  static int buflen;

  char *ptr;
  int i, first;

  if( buflen <= nd_count * 10 )
  {
    buflen = ((nd_count * 10) | 4095) + 1;  // Round up to next page */
    buffer = (char *)realloc( buffer, buflen );
  }
_restart:

  ptr = buffer;
  first = 1;
  *ptr++ = '{';
  for( i=0; i<nd_count; i++ )
  {
    if( !first ) *ptr++ = ',';
    ptr += sprintf(ptr, "%" PRIdOSMID, nds[i] );

    if( (ptr-buffer) > (buflen-20) ) // Almost overflowed? */
    {
      buflen <<= 1;
      buffer = (char *)realloc( buffer, buflen );

      goto _restart;
    }
    first = 0;
  }

  *ptr++ = '}';
  *ptr++ = 0;

  return buffer;
}

// Special escape routine for escaping strings in array constants: double quote, backslash,newline, tab*/
inline char *escape_tag( char *ptr, const char *in, const int& escape )
{
  while( *in )
  {
    switch(*in)
    {
      case '"':
        if( escape ) *ptr++ = '\\';
        *ptr++ = '\\';
        *ptr++ = '"';
        break;
      case '\\':
        if( escape ) *ptr++ = '\\';
        if( escape ) *ptr++ = '\\';
        *ptr++ = '\\';
        *ptr++ = '\\';
        break;
      case '\n':
        if( escape ) *ptr++ = '\\';
        *ptr++ = '\\';
        *ptr++ = 'n';
        break;
      case '\r':
        if( escape ) *ptr++ = '\\';
        *ptr++ = '\\';
        *ptr++ = 'r';
        break;
      case '\t':
        if( escape ) *ptr++ = '\\';
        *ptr++ = '\\';
        *ptr++ = 't';
        break;
      default:
        *ptr++ = *in;
        break;
    }
    in++;
  }
  return ptr;
}

// escape means we return '\N' for copy mode, otherwise we return just NULL */
const char *pgsql_store_tags(const struct keyval *tags, const int& escape)
{
  static char *buffer;
  static int buflen;

  char *ptr;
  int first;

  int countlist = tags->countList();
  if( countlist == 0 )
  {
    if( escape )
      return "\\N";
    else
      return NULL;
  }

  if( buflen <= countlist * 24 ) // LE so 0 always matches */
  {
    buflen = ((countlist * 24) | 4095) + 1;  // Round up to next page */
    buffer = (char *)realloc( buffer, buflen );
  }
_restart:

  ptr = buffer;
  first = 1;
  *ptr++ = '{';

  for(keyval* i = tags->firstItem(); i; i = tags->nextItem(i))
  {
    int maxlen = (i->key.length() + i->value.length()) * 4;
    if( (ptr+maxlen-buffer) > (buflen-20) ) // Almost overflowed? */
    {
      buflen <<= 1;
      buffer = (char *)realloc( buffer, buflen );

      goto _restart;
    }
    if( !first ) *ptr++ = ',';
    *ptr++ = '"';
    ptr = escape_tag( ptr, i->key.c_str(), escape );
    *ptr++ = '"';
    *ptr++ = ',';
    *ptr++ = '"';
    ptr = escape_tag( ptr, i->value.c_str(), escape );
    *ptr++ = '"';

    first=0;
  }

  *ptr++ = '}';
  *ptr++ = 0;

  return buffer;
}

// Decodes a portion of an array literal from postgres */
// Argument should point to beginning of literal, on return points to delimiter */
inline const char *decode_upto( const char *src, char *dst )
{
  int quoted = (*src == '"');
  if( quoted ) src++;

  while( quoted ? (*src != '"') : (*src != ',' && *src != '}') )
  {
    if( *src == '\\' )
    {
      switch( src[1] )
      {
        case 'n': *dst++ = '\n'; break;
        case 't': *dst++ = '\t'; break;
        default: *dst++ = src[1]; break;
      }
      src+=2;
    }
    else
      *dst++ = *src++;
  }
  if( quoted ) src++;
  *dst = 0;
  return src;
}

void pgsql_parse_tags( const char *string, struct keyval *tags )
{
  char key[1024];
  char val[1024];

  if( *string == '\0' )
    return;

  if( *string++ != '{' )
    return;
  while( *string != '}' )
  {
    string = decode_upto( string, key );
    // String points to the comma */
    string++;
    string = decode_upto( string, val );
    // String points to the comma or closing '}' */
    tags->addItem( key, val, false );
    if( *string == ',' )
      string++;
  }
}

// Parses an array of integers */
void pgsql_parse_nodes(const char *src, osmid_t *nds, const int& nd_count )
{
  int count = 0;
  const char *string = src;

  if( *string++ != '{' )
    return;
  while( *string != '}' )
  {
    char *ptr;
    nds[count] = strtoosmid( string, &ptr, 10 );
    string = ptr;
    if( *string == ',' )
      string++;
    count++;
  }
  if( count != nd_count )
  {
    fprintf( stderr, "parse_nodes problem: '%s' expected %d got %d\n", src, nd_count, count );
    util::exit_nicely();
  }
}

int pgsql_endCopy( struct middle_pgsql_t::table_desc *table)
{
    PGresult *res;
    PGconn *sql_conn;
    int stop;
    // Terminate any pending COPY */
    if (table->copyMode) {
        sql_conn = table->sql_conn;
        stop = PQputCopyEnd(sql_conn, NULL);
        if (stop != 1) {
            fprintf(stderr, "COPY_END for %s failed: %s\n", table->copy, PQerrorMessage(sql_conn));
            util::exit_nicely();
        }

        res = PQgetResult(sql_conn);
        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            fprintf(stderr, "COPY_END for %s failed: %s\n", table->copy, PQerrorMessage(sql_conn));
            PQclear(res);
            util::exit_nicely();
        }
        PQclear(res);
        table->copyMode = 0;
    }
    return 0;
}
} // anonymous namespace

int middle_pgsql_t::local_nodes_set(const osmid_t& id, const double& lat, const double& lon, const struct keyval *tags)
{
    // Four params: id, lat, lon, tags */
    const char *paramValues[4];
    char *buffer;

    if( node_table->copyMode )
    {
      const char *tag_buf = pgsql_store_tags(tags,1);
      int length = strlen(tag_buf) + 64;
      buffer = (char *)alloca( length );
#ifdef FIXED_POINT
      if( snprintf( buffer, length, "%" PRIdOSMID "\t%d\t%d\t%s\n", id, util::double_to_fix(lat, out_options->scale), util::double_to_fix(lon, out_options->scale), tag_buf ) > (length-10) )
      { fprintf( stderr, "buffer overflow node id %" PRIdOSMID "\n", id); return 1; }
#else
      if( snprintf( buffer, length, "%" PRIdOSMID "\t%.10f\t%.10f\t%s\n", id, lat, lon, tag_buf ) > (length-10) )
      { fprintf( stderr, "buffer overflow node id %" PRIdOSMID "\n", id); return 1; }
#endif
      pgsql_CopyData(__FUNCTION__, node_table->sql_conn, buffer);
      return 0;
    }
    buffer = (char *)alloca(64);
    char *ptr = buffer;
    paramValues[0] = ptr;
    ptr += sprintf( ptr, "%" PRIdOSMID, id ) + 1;
    paramValues[1] = ptr;
#ifdef FIXED_POINT
    ptr += sprintf( ptr, "%d", util::double_to_fix(lat, out_options->scale) ) + 1;
    paramValues[2] = ptr;
    sprintf( ptr, "%d", util::double_to_fix(lon, out_options->scale) );
#else
    ptr += sprintf( ptr, "%.10f", lat ) + 1;
    paramValues[2] = ptr;
    sprintf( ptr, "%.10f", lon );
#endif
    paramValues[3] = pgsql_store_tags(tags,0);
    pgsql_execPrepared(node_table->sql_conn, "insert_node", 4, (const char * const *)paramValues, PGRES_COMMAND_OK);
    return 0;
}

// This should be made more efficient by using an IN(ARRAY[]) construct */
int middle_pgsql_t::local_nodes_get_list(struct osmNode *nodes, const osmid_t *ndids, const int& nd_count) const
{
    char tmp[16];
    char *tmp2;
    int count,  countDB, countPG, i,j;
    char const *paramValues[1];

    PGresult *res;
    PGconn *sql_conn = node_table->sql_conn;

    count = 0; countDB = 0;

    tmp2 = (char *)malloc(sizeof(char)*nd_count*16);
    if (tmp2 == NULL) return 0; //failed to allocate memory, return */

    // create a list of ids in tmp2 to query the database  */
    sprintf(tmp2, "{");
    for( i=0; i<nd_count; i++ ) {
        // Check cache first */
        if( cache->get( &nodes[i], ndids[i]) == 0 ) {
            count++;
            continue;
        }
        countDB++;
        // Mark nodes as needing to be fetched from the DB */
        nodes[i].lat = NAN;
        nodes[i].lon = NAN;
        snprintf(tmp, sizeof(tmp), "%" PRIdOSMID ",", ndids[i]);
        strncat(tmp2, tmp, sizeof(char)*(nd_count*16 - 2));
    }
    tmp2[strlen(tmp2) - 1] = '}'; // replace last , with } to complete list of ids*/

    if (countDB == 0) {
        free(tmp2);
        return count; // All ids where in cache, so nothing more to do */
    }

    pgsql_endCopy(node_table);

    paramValues[0] = tmp2;
    res = pgsql_execPrepared(sql_conn, "get_node_list", 1, paramValues, PGRES_TUPLES_OK);
    countPG = PQntuples(res);

    //store the pg results in a hashmap and telling it how many we expect
    boost::unordered_map<osmid_t, osmNode> pg_nodes(countPG);

    for (i = 0; i < countPG; i++) {
        osmid_t id = strtoosmid(PQgetvalue(res, i, 0), NULL, 10);
        osmNode node;
#ifdef FIXED_POINT
        node.lat = util::fix_to_double(strtol(PQgetvalue(res, i, 1), NULL, 10), out_options->scale);
        node.lon = util::fix_to_double(strtol(PQgetvalue(res, i, 2), NULL, 10), out_options->scale);
#else
        node.lat = strtod(PQgetvalue(res, i, 1), NULL);
        node.lon = strtod(PQgetvalue(res, i, 2), NULL);
#endif
        pg_nodes.emplace(id, node);
    }

    //copy the nodes back out of the hashmap to the output
    for(i = 0; i < nd_count; ++i){
        //if we can find a matching id
        boost::unordered_map<osmid_t, osmNode>::iterator found = pg_nodes.find(ndids[i]);
        if(found != pg_nodes.end()) {
            nodes[i] = boost::move(found->second); //this trashes whats in the hashmap but who cares
            count++;
        }
    }

    // If some of the nodes in the way don't exist, the returning list has holes.
    //   As the rest of the code expects a continuous list, it needs to be re-compacted */
    if (count != nd_count) {
        j = 0;
        for (i = 0; i < nd_count; i++) {
            if ( !std::isnan(nodes[i].lat)) {
                nodes[j] = nodes[i];
                j++;
            }
         }
    }

    PQclear(res);
    free(tmp2);
    return count;
}


int middle_pgsql_t::nodes_set(osmid_t id, double lat, double lon, struct keyval *tags) {
    cache->set( id, lat, lon, tags );

    return (out_options->flat_node_cache_enabled) ? persistent_cache->set(id, lat, lon) : local_nodes_set(id, lat, lon, tags);
}

int middle_pgsql_t::nodes_get_list(struct osmNode *nodes, const osmid_t *ndids, int nd_count) const
{
    return (out_options->flat_node_cache_enabled) ? persistent_cache->get_list(nodes, ndids, nd_count) : local_nodes_get_list(nodes, ndids, nd_count);
}

int middle_pgsql_t::local_nodes_delete(osmid_t osm_id)
{
    char const *paramValues[1];
    char buffer[64];
    // Make sure we're out of copy mode */
    pgsql_endCopy( node_table );

    sprintf( buffer, "%" PRIdOSMID, osm_id );
    paramValues[0] = buffer;
    pgsql_execPrepared(node_table->sql_conn, "delete_node", 1, paramValues, PGRES_COMMAND_OK );
    return 0;
}

int middle_pgsql_t::nodes_delete(osmid_t osm_id)
{
    return ((out_options->flat_node_cache_enabled) ? persistent_cache->set(osm_id, NAN, NAN) : local_nodes_delete(osm_id));
}

int middle_pgsql_t::node_changed(osmid_t osm_id)
{
    char const *paramValues[1];
    char buffer[64];
    // Make sure we're out of copy mode */
    pgsql_endCopy( way_table );
    pgsql_endCopy( rel_table );

    sprintf( buffer, "%" PRIdOSMID, osm_id );
    paramValues[0] = buffer;

    //keep track of whatever ways and rels these nodes intersect
    //TODO: dont need to stop the copy above since we are only reading?
    PGresult* res = pgsql_execPrepared(way_table->sql_conn, "mark_ways_by_node", 1, paramValues, PGRES_TUPLES_OK );
    for(int i = 0; i < PQntuples(res); ++i)
    {
        char *end;
        osmid_t marked = strtoosmid(PQgetvalue(res, i, 0), &end, 10);
        ways_pending_tracker->mark(marked);
    }
    PQclear(res);

    //do the rels too
    res = pgsql_execPrepared(rel_table->sql_conn, "mark_rels_by_node", 1, paramValues, PGRES_TUPLES_OK );
    for(int i = 0; i < PQntuples(res); ++i)
    {
        char *end;
        osmid_t marked = strtoosmid(PQgetvalue(res, i, 0), &end, 10);
        rels_pending_tracker->mark(marked);
    }
    PQclear(res);

    return 0;
}

int middle_pgsql_t::ways_set(osmid_t way_id, osmid_t *nds, int nd_count, struct keyval *tags)
{
    // Three params: id, nodes, tags */
    const char *paramValues[4];
    char *buffer;

    if( way_table->copyMode )
    {
      const char *tag_buf = pgsql_store_tags(tags,1);
      char *node_buf = pgsql_store_nodes(nds, nd_count);
      int length = strlen(tag_buf) + strlen(node_buf) + 64;
      buffer = (char *)alloca(length);
      if( snprintf( buffer, length, "%" PRIdOSMID "\t%s\t%s\n",
              way_id, node_buf, tag_buf ) > (length-10) )
      { fprintf( stderr, "buffer overflow way id %" PRIdOSMID "\n", way_id); return 1; }
      pgsql_CopyData(__FUNCTION__, way_table->sql_conn, buffer);
      return 0;
    }
    buffer = (char *)alloca(64);
    char *ptr = buffer;
    paramValues[0] = ptr;
    sprintf(ptr, "%" PRIdOSMID, way_id);
    paramValues[1] = pgsql_store_nodes(nds, nd_count);
    paramValues[2] = pgsql_store_tags(tags,0);
    pgsql_execPrepared(way_table->sql_conn, "insert_way", 3, (const char * const *)paramValues, PGRES_COMMAND_OK);
    return 0;
}

// Caller is responsible for freeing nodesptr & keyval::resetList(tags) */
int middle_pgsql_t::ways_get(osmid_t id, struct keyval *tags, struct osmNode **nodes_ptr, int *count_ptr) const
{
    PGresult   *res;
    char tmp[16];
    char const *paramValues[1];
    PGconn *sql_conn = way_table->sql_conn;
    int num_nodes;
    osmid_t *list;

    // Make sure we're out of copy mode */
    pgsql_endCopy( way_table );

    snprintf(tmp, sizeof(tmp), "%" PRIdOSMID, id);
    paramValues[0] = tmp;

    res = pgsql_execPrepared(sql_conn, "get_way", 1, paramValues, PGRES_TUPLES_OK);

    if (PQntuples(res) != 1) {
        PQclear(res);
        return 1;
    }

    pgsql_parse_tags( PQgetvalue(res, 0, 1), tags );

    num_nodes = strtol(PQgetvalue(res, 0, 2), NULL, 10);
    list = (osmid_t *)alloca(sizeof(osmid_t)*num_nodes );
    *nodes_ptr = (struct osmNode *)malloc(sizeof(struct osmNode) * num_nodes);
    pgsql_parse_nodes( PQgetvalue(res, 0, 0), list, num_nodes);

    *count_ptr = nodes_get_list(*nodes_ptr, list, num_nodes);
    PQclear(res);
    return 0;
}

int middle_pgsql_t::ways_get_list(const osmid_t *ids, int way_count, osmid_t *way_ids, struct keyval *tags, struct osmNode **nodes_ptr, int *count_ptr) const {

    char tmp[16];
    char *tmp2;
    int count, countPG, i, j;
    osmid_t *wayidspg;
    char const *paramValues[1];
    int num_nodes;
    osmid_t *list;

    PGresult *res;
    PGconn *sql_conn = way_table->sql_conn;

    if (way_count == 0) return 0;

    tmp2 = (char *)malloc(sizeof(char)*way_count*16);
    if (tmp2 == NULL) return 0; //failed to allocate memory, return */

    // create a list of ids in tmp2 to query the database  */
    sprintf(tmp2, "{");
    for( i=0; i<way_count; i++ ) {
        snprintf(tmp, sizeof(tmp), "%" PRIdOSMID ",", ids[i]);
        strncat(tmp2,tmp, sizeof(char)*(way_count*16 - 2));
    }
    tmp2[strlen(tmp2) - 1] = '}'; // replace last , with } to complete list of ids*/

    pgsql_endCopy(way_table);

    paramValues[0] = tmp2;
    res = pgsql_execPrepared(sql_conn, "get_way_list", 1, paramValues, PGRES_TUPLES_OK);
    countPG = PQntuples(res);

    wayidspg = (osmid_t *)malloc(sizeof(osmid_t)*countPG);

    if (wayidspg == NULL) return 0; //failed to allocate memory, return */

    for (i = 0; i < countPG; i++) {
        wayidspg[i] = strtoosmid(PQgetvalue(res, i, 0), NULL, 10);
    }


    // Match the list of ways coming from postgres in a different order
    //   back to the list of ways given by the caller */
    count = 0;
    for (i = 0; i < way_count; i++) {
        for (j = 0; j < countPG; j++) {
            if (ids[i] == wayidspg[j]) {
                way_ids[count] = ids[i];
                pgsql_parse_tags( PQgetvalue(res, j, 2), &(tags[count]) );

                num_nodes = strtol(PQgetvalue(res, j, 3), NULL, 10);
                list = (osmid_t *)alloca(sizeof(osmid_t)*num_nodes );
                nodes_ptr[count] = (struct osmNode *)malloc(sizeof(struct osmNode) * num_nodes);
                pgsql_parse_nodes( PQgetvalue(res, j, 1), list, num_nodes);

                count_ptr[count] = nodes_get_list(nodes_ptr[count], list, num_nodes);

                count++;
                break;
            }
        }
    }

    assert(count<=way_count);

    PQclear(res);
    free(tmp2);
    free(wayidspg);

    return count;
}


int middle_pgsql_t::ways_delete(osmid_t osm_id)
{
    char const *paramValues[1];
    char buffer[64];
    // Make sure we're out of copy mode */
    pgsql_endCopy( way_table );

    sprintf( buffer, "%" PRIdOSMID, osm_id );
    paramValues[0] = buffer;
    pgsql_execPrepared(way_table->sql_conn, "delete_way", 1, paramValues, PGRES_COMMAND_OK );
    return 0;
}

void middle_pgsql_t::iterate_ways(middle_t::pending_processor& pf)
{

    // Make sure we're out of copy mode */
    pgsql_endCopy( way_table );

    // enqueue the jobs
    osmid_t id;
    while(id_tracker::is_valid(id = ways_pending_tracker->pop_mark()))
    {
        pf.enqueue_ways(id);
    }
    // in case we had higher ones than the middle
    pf.enqueue_ways(id_tracker::max());

    //let the threads work on them
    pf.process_ways();
}

int middle_pgsql_t::way_changed(osmid_t osm_id)
{
    char const *paramValues[1];
    char buffer[64];
    // Make sure we're out of copy mode */
    pgsql_endCopy( rel_table );

    sprintf( buffer, "%" PRIdOSMID, osm_id );
    paramValues[0] = buffer;

    //keep track of whatever rels this way intersects
    //TODO: dont need to stop the copy above since we are only reading?
    PGresult* res = pgsql_execPrepared(rel_table->sql_conn, "mark_rels_by_way", 1, paramValues, PGRES_TUPLES_OK );
    for(int i = 0; i < PQntuples(res); ++i)
    {
        char *end;
        osmid_t marked = strtoosmid(PQgetvalue(res, i, 0), &end, 10);
        rels_pending_tracker->mark(marked);
    }
    PQclear(res);

    return 0;
}

int middle_pgsql_t::relations_set(osmid_t id, struct member *members, int member_count, struct keyval *tags)
{
    // Params: id, way_off, rel_off, parts, members, tags */
    const char *paramValues[6];
    char *buffer;
    int i;
    struct keyval member_list;
    char buf[64];

    int node_count = 0, way_count = 0, rel_count = 0;

    std::vector<osmid_t> all_parts(member_count), node_parts, way_parts, rel_parts;
    node_parts.reserve(member_count);
    way_parts.reserve(member_count);
    rel_parts.reserve(member_count);

    for( i=0; i<member_count; i++ )
    {
      char tag = 0;
      switch( members[i].type )
      {
        case OSMTYPE_NODE:     node_count++; node_parts.push_back(members[i].id); tag = 'n'; break;
        case OSMTYPE_WAY:      way_count++; way_parts.push_back(members[i].id); tag = 'w'; break;
        case OSMTYPE_RELATION: rel_count++; rel_parts.push_back(members[i].id); tag = 'r'; break;
        default: fprintf( stderr, "Internal error: Unknown member type %d\n", members[i].type ); util::exit_nicely();
      }
      sprintf( buf, "%c%" PRIdOSMID, tag, members[i].id );
      member_list.addItem( buf, members[i].role, false );
    }

    int all_count = 0;
    std::copy( node_parts.begin(), node_parts.end(), all_parts.begin() );
    std::copy( way_parts.begin(), way_parts.end(), all_parts.begin() + node_count );
    std::copy( rel_parts.begin(), rel_parts.end(), all_parts.begin() + node_count + way_count);
    all_count = node_count + way_count + rel_count;

    if( rel_table->copyMode )
    {
      char *tag_buf = strdup(pgsql_store_tags(tags,1));
      const char *member_buf = pgsql_store_tags(&member_list,1);
      char *parts_buf = pgsql_store_nodes(&all_parts[0], all_count);
      int length = strlen(member_buf) + strlen(tag_buf) + strlen(parts_buf) + 64;
      buffer = (char *)alloca(length);
      if( snprintf( buffer, length, "%" PRIdOSMID "\t%d\t%d\t%s\t%s\t%s\n",
              id, node_count, node_count+way_count, parts_buf, member_buf, tag_buf ) > (length-10) )
      { fprintf( stderr, "buffer overflow relation id %" PRIdOSMID "\n", id); return 1; }
      free(tag_buf);
      member_list.resetList();
      pgsql_CopyData(__FUNCTION__, rel_table->sql_conn, buffer);
      return 0;
    }
    buffer = (char *)alloca(64);
    char *ptr = buffer;
    paramValues[0] = ptr;
    ptr += sprintf(ptr, "%" PRIdOSMID, id ) + 1;
    paramValues[1] = ptr;
    ptr += sprintf(ptr, "%d", node_count ) + 1;
    paramValues[2] = ptr;
    sprintf( ptr, "%d", node_count+way_count );
    paramValues[3] = pgsql_store_nodes(&all_parts[0], all_count);
    paramValues[4] = pgsql_store_tags(&member_list,0);
    if( paramValues[4] )
        paramValues[4] = strdup(paramValues[4]);
    paramValues[5] = pgsql_store_tags(tags,0);
    pgsql_execPrepared(rel_table->sql_conn, "insert_rel", 6, (const char * const *)paramValues, PGRES_COMMAND_OK);
    if( paramValues[4] )
        free((void *)paramValues[4]);
    member_list.resetList();
    return 0;
}

// Caller is responsible for freeing members & keyval::resetList(tags) */
int middle_pgsql_t::relations_get(osmid_t id, struct member **members, int *member_count, struct keyval *tags) const
{
    PGresult   *res;
    char tmp[16];
    char const *paramValues[1];
    PGconn *sql_conn = rel_table->sql_conn;
    struct keyval member_temp;
    char tag;
    int num_members;
    struct member *list;
    int i=0;

    // Make sure we're out of copy mode */
    pgsql_endCopy( rel_table );

    snprintf(tmp, sizeof(tmp), "%" PRIdOSMID, id);
    paramValues[0] = tmp;

    res = pgsql_execPrepared(sql_conn, "get_rel", 1, paramValues, PGRES_TUPLES_OK);
    // Fields are: members, tags, member_count */

    if (PQntuples(res) != 1) {
        PQclear(res);
        return 1;
    }

    pgsql_parse_tags( PQgetvalue(res, 0, 1), tags );
    pgsql_parse_tags( PQgetvalue(res, 0, 0), &member_temp );

    num_members = strtol(PQgetvalue(res, 0, 2), NULL, 10);
    list = (struct member *)malloc( sizeof(struct member)*num_members );

    keyval *item;
    while((item = member_temp.popItem()))
    {
        if( i >= num_members )
        {
            fprintf(stderr, "Unexpected member_count reading relation %" PRIdOSMID "\n", id);
            util::exit_nicely();
        }
        tag = item->key[0];
        list[i].type = (tag == 'n')?OSMTYPE_NODE:(tag == 'w')?OSMTYPE_WAY:(tag == 'r')?OSMTYPE_RELATION:((OsmType)-1);
        list[i].id = strtoosmid(item->key.c_str()+1, NULL, 10 );
        list[i].role = strdup( item->value.c_str() );
        delete(item);
        i++;
    }
    *members = list;
    *member_count = num_members;
    PQclear(res);
    return 0;
}

int middle_pgsql_t::relations_delete(osmid_t osm_id)
{
    char const *paramValues[1];
    char buffer[64];
    // Make sure we're out of copy mode */
    pgsql_endCopy( way_table );
    pgsql_endCopy( rel_table );

    sprintf( buffer, "%" PRIdOSMID, osm_id );
    paramValues[0] = buffer;
    pgsql_execPrepared(rel_table->sql_conn, "delete_rel", 1, paramValues, PGRES_COMMAND_OK );

    //keep track of whatever ways this relation interesects
    //TODO: dont need to stop the copy above since we are only reading?
    PGresult* res = pgsql_execPrepared(way_table->sql_conn, "mark_ways_by_rel", 1, paramValues, PGRES_TUPLES_OK );
    for(int i = 0; i < PQntuples(res); ++i)
    {
        char *end;
        osmid_t marked = strtoosmid(PQgetvalue(res, i, 0), &end, 10);
        ways_pending_tracker->mark(marked);
    }
    PQclear(res);
    return 0;
}

void middle_pgsql_t::iterate_relations(pending_processor& pf)
{
    // Make sure we're out of copy mode */
    pgsql_endCopy( rel_table );

    // enqueue the jobs
    osmid_t id;
    while(id_tracker::is_valid(id = rels_pending_tracker->pop_mark()))
    {
        pf.enqueue_relations(id);
    }
    // in case we had higher ones than the middle
    pf.enqueue_relations(id_tracker::max());

    //let the threads work on them
    pf.process_relations();
}

int middle_pgsql_t::relation_changed(osmid_t osm_id)
{
    char const *paramValues[1];
    char buffer[64];
    // Make sure we're out of copy mode */
    pgsql_endCopy( rel_table );

    sprintf( buffer, "%" PRIdOSMID, osm_id );
    paramValues[0] = buffer;

    //keep track of whatever ways and rels these nodes intersect
    //TODO: dont need to stop the copy above since we are only reading?
    //TODO: can we just mark the id without querying? the where clause seems intersect reltable.parts with the id
    PGresult* res = pgsql_execPrepared(rel_table->sql_conn, "mark_rels", 1, paramValues, PGRES_TUPLES_OK );
    for(int i = 0; i < PQntuples(res); ++i)
    {
        char *end;
        osmid_t marked = strtoosmid(PQgetvalue(res, i, 0), &end, 10);
        rels_pending_tracker->mark(marked);
    }
    PQclear(res);
    return 0;
}

std::vector<osmid_t> middle_pgsql_t::relations_using_way(osmid_t way_id) const
{
    char const *paramValues[1];
    char buffer[64];
    // Make sure we're out of copy mode */
    pgsql_endCopy( rel_table );

    sprintf(buffer, "%" PRIdOSMID, way_id);
    paramValues[0] = buffer;

    PGresult *result = pgsql_execPrepared(rel_table->sql_conn, "rels_using_way",
                                          1, paramValues, PGRES_TUPLES_OK );
    const int ntuples = PQntuples(result);
    std::vector<osmid_t> rel_ids(ntuples);
    for (int i = 0; i < ntuples; ++i) {
        rel_ids[i] = strtoosmid(PQgetvalue(result, i, 0), NULL, 10);
    }
    PQclear(result);

    return rel_ids;
}

void middle_pgsql_t::analyze(void)
{
    int i;

    for (i=0; i<num_tables; i++) {
        PGconn *sql_conn = tables[i].sql_conn;

        if (tables[i].analyze) {
            pgsql_exec(sql_conn, PGRES_COMMAND_OK, "%s", tables[i].analyze);
        }
    }
}

void middle_pgsql_t::end(void)
{
    int i;

    for (i=0; i<num_tables; i++) {
        PGconn *sql_conn = tables[i].sql_conn;

        // Commit transaction */
        if (tables[i].stop && tables[i].transactionMode) {
            pgsql_exec(sql_conn, PGRES_COMMAND_OK, "%s", tables[i].stop);
            tables[i].transactionMode = 0;
        }

    }
}

/**
 * Helper to create SQL queries.
 *
 * The input string is mangled as follows:
 * %p replaced by the content of the "prefix" option
 * %i replaced by the content of the "tblsslim_data" option
 * %t replaced by the content of the "tblssslim_index" option
 * %m replaced by "UNLOGGED" if the "unlogged" option is set
 * other occurrences of the "%" char are treated normally.
 * any occurrence of { or } will be ignored (not copied to output string);
 * anything inside {} is only copied if it contained at least one of
 * %p, %i, %t, %m that was not NULL.
 *
 * So, the input string
 *    Hello{ dear %i}!
 * will, if i is set to "John", translate to
 *    Hello dear John!
 * but if i is unset, translate to
 *    Hello!
 *
 * This is used for constructing SQL queries with proper tablespace settings.
 */
static void set_prefix_and_tbls(const struct options_t *options, const char **string)
{
    char buffer[1024];
    const char *source;
    char *dest;
    char *openbrace = NULL;
    int copied = 0;

    if (*string == NULL) return;
    source = *string;
    dest = buffer;

    while (*source) {
        if (*source == '{') {
            openbrace = dest;
            copied = 0;
            source++;
            continue;
        } else if (*source == '}') {
            if (!copied && openbrace) dest = openbrace;
            source++;
            continue;
        } else if (*source == '%') {
            if (*(source+1) == 'p') {
                if (!options->prefix.empty()) {
                    strcpy(dest, options->prefix.c_str());
                    dest += strlen(options->prefix.c_str());
                    copied = 1;
                }
                source+=2;
                continue;
            } else if (*(source+1) == 't') {
                if (options->tblsslim_data) {
                    strcpy(dest, options->tblsslim_data->c_str());
                    dest += strlen(options->tblsslim_data->c_str());
                    copied = 1;
                }
                source+=2;
                continue;
            } else if (*(source+1) == 'i') {
                if (options->tblsslim_index) {
                    strcpy(dest, options->tblsslim_index->c_str());
                    dest += strlen(options->tblsslim_index->c_str());
                    copied = 1;
                }
                source+=2;
                continue;
            } else if (*(source+1) == 'm') {
                if (options->unlogged) {
                    strcpy(dest, "UNLOGGED");
                    dest += 8;
                    copied = 1;
                }
                source+=2;
                continue;
            }
        }
        *(dest++) = *(source++);
    }
    *dest = 0;
    *string = strdup(buffer);
}

int middle_pgsql_t::connect(table_desc& table) {
    PGconn *sql_conn;

    set_prefix_and_tbls(out_options, &(table.name));
    set_prefix_and_tbls(out_options, &(table.start));
    set_prefix_and_tbls(out_options, &(table.create));
    set_prefix_and_tbls(out_options, &(table.create_index));
    set_prefix_and_tbls(out_options, &(table.prepare));
    set_prefix_and_tbls(out_options, &(table.prepare_intarray));
    set_prefix_and_tbls(out_options, &(table.copy));
    set_prefix_and_tbls(out_options, &(table.analyze));
    set_prefix_and_tbls(out_options, &(table.stop));
    set_prefix_and_tbls(out_options, &(table.array_indexes));

    fprintf(stderr, "Setting up table: %s\n", table.name);
    sql_conn = PQconnectdb(out_options->conninfo.c_str());

    // Check to see that the backend connection was successfully made */
    if (PQstatus(sql_conn) != CONNECTION_OK) {
        fprintf(stderr, "Connection to database failed: %s\n", PQerrorMessage(sql_conn));
        return 1;
    }
    table.sql_conn = sql_conn;
    return 0;
}

int middle_pgsql_t::start(const options_t *out_options_)
{
    out_options = out_options_;
    PGresult   *res;
    int i;
    int dropcreate = !out_options->append;
    char * sql;

    ways_pending_tracker.reset(new id_tracker());
    rels_pending_tracker.reset(new id_tracker());

    Append = out_options->append;
    // reset this on every start to avoid options from last run
    // staying set for the second.
    build_indexes = 0;

    cache.reset(new node_ram_cache( out_options->alloc_chunkwise | ALLOC_LOSSY, out_options->cache, out_options->scale));
    if (out_options->flat_node_cache_enabled) persistent_cache.reset(new node_persistent_cache(out_options, out_options->append, cache));

    fprintf(stderr, "Mid: pgsql, scale=%d cache=%d\n", out_options->scale, out_options->cache);

    // We use a connection per table to enable the use of COPY */
    for (i=0; i<num_tables; i++) {
        //bomb if you cant connect
        if(connect(tables[i]))
            util::exit_nicely();
        PGconn* sql_conn = tables[i].sql_conn;

        /*
         * To allow for parallelisation, the second phase (iterate_ways), cannot be run
         * in an extended transaction and each update statement is its own transaction.
         * Therefore commit rate of postgresql is very important to ensure high speed.
         * If fsync is enabled to ensure safe transactions, the commit rate can be very low.
         * To compensate for this, one can set the postgresql parameter synchronous_commit
         * to off. This means an update statement returns to the client as success before the
         * transaction is saved to disk via fsync, which in return allows to bunch up multiple
         * transactions into a single fsync. This may result in some data loss in the case of a
         * database crash. However, as we don't currently have the ability to restart a full osm2pgsql
         * import session anyway, this is fine. Diff imports are also not effected, as the next
         * diff import would simply deal with all pending ways that were not previously finished.
         * This parameter does not effect safety from data corruption on the back-end.
         */
        pgsql_exec(sql_conn, PGRES_COMMAND_OK, "SET synchronous_commit TO off;");

        /* Not really the right place for this test, but we need a live
         * connection that not used for anything else yet, and we'd like to
         * warn users *before* we start doing mountains of work */
        if (i == t_node)
        {
            res = PQexec(sql_conn, "select 1 from pg_opclass where opcname='gist__intbig_ops'" );
            if(PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) == 1)
            {
                /* intarray is problematic now; causes at least postgres 8.4
                 * to not use the index on nodes[]/parts[] which slows diff
                 * updates to a crawl!
                 * If someone find a way to fix this rather than bow out here,
                 * please do.*/

                fprintf(stderr,
                    "\n"
                    "The target database has the intarray contrib module loaded.\n"
                    "While required for earlier versions of osm2pgsql, intarray \n"
                    "is now unnecessary and will interfere with osm2pgsql's array\n"
                    "handling. Please use a database without intarray.\n\n");
                util::exit_nicely();
            }
            PQclear(res);

            if (out_options->append)
            {
                sql = (char *)malloc (2048);
                snprintf(sql, 2047, "SELECT id FROM %s LIMIT 1", tables[t_node].name);
                res = PQexec(sql_conn, sql );
                free(sql);
                sql = NULL;
                if(PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) == 1)
                {
                    int size = PQfsize(res, 0);
                    if (size != sizeof(osmid_t))
                    {
                        fprintf(stderr,
                            "\n"
                            "The target database has been created with %dbit ID fields,\n"
                            "but this version of osm2pgsql has been compiled to use %ldbit IDs.\n"
                            "You cannot append data to this database with this program.\n"
                            "Either re-create the database or use a matching osm2pgsql.\n\n",
                            size * 8, sizeof(osmid_t) * 8);
                        util::exit_nicely();
                    }
                }
                PQclear(res);
            }

            if(!out_options->append)
                build_indexes = 1;
        }

        pgsql_exec(sql_conn, PGRES_COMMAND_OK, "SET client_min_messages = WARNING");
        if (dropcreate) {
            pgsql_exec(sql_conn, PGRES_COMMAND_OK, "DROP TABLE IF EXISTS %s", tables[i].name);
        }

        if (tables[i].start) {
            pgsql_exec(sql_conn, PGRES_COMMAND_OK, "%s", tables[i].start);
            tables[i].transactionMode = 1;
        }

        if (dropcreate && tables[i].create) {
            pgsql_exec(sql_conn, PGRES_COMMAND_OK, "%s", tables[i].create);
            if (tables[i].create_index) {
              pgsql_exec(sql_conn, PGRES_COMMAND_OK, "%s", tables[i].create_index);
            }
        }
        pgsql_exec(sql_conn, PGRES_COMMAND_OK, "RESET client_min_messages");

        if (tables[i].prepare) {
            pgsql_exec(sql_conn, PGRES_COMMAND_OK, "%s", tables[i].prepare);
        }

        if (Append && tables[i].prepare_intarray) {
            pgsql_exec(sql_conn, PGRES_COMMAND_OK, "%s", tables[i].prepare_intarray);
        }

        if (tables[i].copy) {
            pgsql_exec(sql_conn, PGRES_COPY_IN, "%s", tables[i].copy);
            tables[i].copyMode = 1;
        }
    }

    return 0;
}

void middle_pgsql_t::commit(void) {
    int i;
    for (i=0; i<num_tables; i++) {
        PGconn *sql_conn = tables[i].sql_conn;
        pgsql_endCopy(&tables[i]);
        if (tables[i].stop && tables[i].transactionMode) {
            pgsql_exec(sql_conn, PGRES_COMMAND_OK, "%s", tables[i].stop);
            tables[i].transactionMode = 0;
        }
    }
    // Make sure the flat nodes are committed to disk or there will be
    // surprises later.
    if (out_options->flat_node_cache_enabled) persistent_cache.reset();
}

void *middle_pgsql_t::pgsql_stop_one(void *arg)
{
    time_t start, end;

    struct table_desc *table = (struct table_desc *)arg;
    PGconn *sql_conn = table->sql_conn;

    fprintf(stderr, "Stopping table: %s\n", table->name);
    pgsql_endCopy(table);
    time(&start);
    if (out_options->droptemp)
    {
        pgsql_exec(sql_conn, PGRES_COMMAND_OK, "DROP TABLE %s", table->name);
    }
    else if (build_indexes && table->array_indexes)
    {
        fprintf(stderr, "Building index on table: %s\n", table->name);
        pgsql_exec(sql_conn, PGRES_COMMAND_OK, "%s", table->array_indexes);
    }

    PQfinish(sql_conn);
    table->sql_conn = NULL;
    time(&end);
    fprintf(stderr, "Stopped table: %s in %is\n", table->name, (int)(end - start));
    return NULL;
}

namespace {
/* Using pthreads requires us to shoe-horn everything into various void*
 * pointers. Improvement for the future: just use boost::thread. */
struct pthread_thunk {
    middle_pgsql_t *obj;
    void *ptr;
};

extern "C" void *pthread_middle_pgsql_stop_one(void *arg) {
    pthread_thunk *thunk = static_cast<pthread_thunk *>(arg);
    return thunk->obj->pgsql_stop_one(thunk->ptr);
}
} // anonymous namespace

void middle_pgsql_t::stop(void)
{
    int i;
#ifdef HAVE_PTHREAD
    pthread_t threads[num_tables];
#endif

    cache.reset();
    if (out_options->flat_node_cache_enabled) persistent_cache.reset();

#ifdef HAVE_PTHREAD
    pthread_thunk thunks[num_tables];
    for (i=0; i<num_tables; i++) {
        thunks[i].obj = this;
        thunks[i].ptr = &tables[i];
    }

    for (i=0; i<num_tables; i++) {
        int ret = pthread_create(&threads[i], NULL, pthread_middle_pgsql_stop_one, &thunks[i]);
        if (ret) {
            fprintf(stderr, "pthread_create() returned an error (%d)", ret);
            util::exit_nicely();
        }
    }

    for (i=0; i<num_tables; i++) {
        int ret = pthread_join(threads[i], NULL);
        if (ret) {
            fprintf(stderr, "pthread_join() returned an error (%d)", ret);
            util::exit_nicely();
        }
    }
#else
    for (i=0; i<num_tables; i++)
        pgsql_stop_one(&tables[i]);
#endif
}

middle_pgsql_t::middle_pgsql_t()
    : tables(), num_tables(0), node_table(NULL), way_table(NULL), rel_table(NULL),
      Append(0), cache(), persistent_cache(), build_indexes(0)
{
    /*table = t_node,*/
    tables.push_back(table_desc(
            /*name*/ "%p_nodes",
           /*start*/ "BEGIN;\n",
#ifdef FIXED_POINT
          /*create*/ "CREATE %m TABLE %p_nodes (id " POSTGRES_OSMID_TYPE " PRIMARY KEY {USING INDEX TABLESPACE %i}, lat int4 not null, lon int4 not null, tags text[]) {TABLESPACE %t};\n",
    /*create_index*/ NULL,
         /*prepare*/ "PREPARE insert_node (" POSTGRES_OSMID_TYPE ", int4, int4, text[]) AS INSERT INTO %p_nodes VALUES ($1,$2,$3,$4);\n"
#else
          /*create*/ "CREATE %m TABLE %p_nodes (id " POSTGRES_OSMID_TYPE " PRIMARY KEY {USING INDEX TABLESPACE %i}, lat double precision not null, lon double precision not null, tags text[]) {TABLESPACE %t};\n",
    /*create_index*/ NULL,
         /*prepare*/ "PREPARE insert_node (" POSTGRES_OSMID_TYPE ", double precision, double precision, text[]) AS INSERT INTO %p_nodes VALUES ($1,$2,$3,$4);\n"
#endif
               "PREPARE get_node (" POSTGRES_OSMID_TYPE ") AS SELECT lat,lon,tags FROM %p_nodes WHERE id = $1 LIMIT 1;\n"
               "PREPARE get_node_list(" POSTGRES_OSMID_TYPE "[]) AS SELECT id, lat, lon FROM %p_nodes WHERE id = ANY($1::" POSTGRES_OSMID_TYPE "[]);\n"
               "PREPARE delete_node (" POSTGRES_OSMID_TYPE ") AS DELETE FROM %p_nodes WHERE id = $1;\n",
/*prepare_intarray*/ NULL,
            /*copy*/ "COPY %p_nodes FROM STDIN;\n",
         /*analyze*/ "ANALYZE %p_nodes;\n",
            /*stop*/ "COMMIT;\n"
                         ));
    tables.push_back(table_desc(
        /*table t_way,*/
            /*name*/ "%p_ways",
           /*start*/ "BEGIN;\n",
          /*create*/ "CREATE %m TABLE %p_ways (id " POSTGRES_OSMID_TYPE " PRIMARY KEY {USING INDEX TABLESPACE %i}, nodes " POSTGRES_OSMID_TYPE "[] not null, tags text[]) {TABLESPACE %t};\n",
    /*create_index*/ NULL,
         /*prepare*/ "PREPARE insert_way (" POSTGRES_OSMID_TYPE ", " POSTGRES_OSMID_TYPE "[], text[]) AS INSERT INTO %p_ways VALUES ($1,$2,$3);\n"
               "PREPARE get_way (" POSTGRES_OSMID_TYPE ") AS SELECT nodes, tags, array_upper(nodes,1) FROM %p_ways WHERE id = $1;\n"
               "PREPARE get_way_list (" POSTGRES_OSMID_TYPE "[]) AS SELECT id, nodes, tags, array_upper(nodes,1) FROM %p_ways WHERE id = ANY($1::" POSTGRES_OSMID_TYPE "[]);\n"
               "PREPARE delete_way(" POSTGRES_OSMID_TYPE ") AS DELETE FROM %p_ways WHERE id = $1;\n",
/*prepare_intarray*/
               "PREPARE mark_ways_by_node(" POSTGRES_OSMID_TYPE ") AS select id from %p_ways WHERE nodes && ARRAY[$1];\n"
               "PREPARE mark_ways_by_rel(" POSTGRES_OSMID_TYPE ") AS select id from %p_ways WHERE id IN (SELECT unnest(parts[way_off+1:rel_off]) FROM %p_rels WHERE id = $1);\n",

            /*copy*/ "COPY %p_ways FROM STDIN;\n",
         /*analyze*/ "ANALYZE %p_ways;\n",
            /*stop*/  "COMMIT;\n",
   /*array_indexes*/ "CREATE INDEX %p_ways_nodes ON %p_ways USING gin (nodes) WITH (FASTUPDATE=OFF) {TABLESPACE %i};\n"
                         ));
    tables.push_back(table_desc(
        /*table = t_rel,*/
            /*name*/ "%p_rels",
           /*start*/ "BEGIN;\n",
          /*create*/ "CREATE %m TABLE %p_rels(id " POSTGRES_OSMID_TYPE " PRIMARY KEY {USING INDEX TABLESPACE %i}, way_off int2, rel_off int2, parts " POSTGRES_OSMID_TYPE "[], members text[], tags text[]) {TABLESPACE %t};\n",
    /*create_index*/ NULL,
         /*prepare*/ "PREPARE insert_rel (" POSTGRES_OSMID_TYPE ", int2, int2, " POSTGRES_OSMID_TYPE "[], text[], text[]) AS INSERT INTO %p_rels VALUES ($1,$2,$3,$4,$5,$6);\n"
               "PREPARE get_rel (" POSTGRES_OSMID_TYPE ") AS SELECT members, tags, array_upper(members,1)/2 FROM %p_rels WHERE id = $1;\n"
               "PREPARE delete_rel(" POSTGRES_OSMID_TYPE ") AS DELETE FROM %p_rels WHERE id = $1;\n",
/*prepare_intarray*/
                "PREPARE rels_using_way(" POSTGRES_OSMID_TYPE ") AS SELECT id FROM %p_rels WHERE parts && ARRAY[$1] AND parts[way_off+1:rel_off] && ARRAY[$1];\n"
                "PREPARE mark_rels_by_node(" POSTGRES_OSMID_TYPE ") AS select id from %p_ways WHERE nodes && ARRAY[$1];\n"
                "PREPARE mark_rels_by_way(" POSTGRES_OSMID_TYPE ") AS select id from %p_rels WHERE parts && ARRAY[$1] AND parts[way_off+1:rel_off] && ARRAY[$1];\n"
                "PREPARE mark_rels(" POSTGRES_OSMID_TYPE ") AS select id from %p_rels WHERE parts && ARRAY[$1] AND parts[rel_off+1:array_length(parts,1)] && ARRAY[$1];\n",

            /*copy*/ "COPY %p_rels FROM STDIN;\n",
         /*analyze*/ "ANALYZE %p_rels;\n",
            /*stop*/  "COMMIT;\n",
   /*array_indexes*/ "CREATE INDEX %p_rels_parts ON %p_rels USING gin (parts) WITH (FASTUPDATE=OFF) {TABLESPACE %i};\n"
                         ));

    // set up the rest of the variables from the tables.
    num_tables = tables.size();
    assert(num_tables == 3);

    node_table = &tables[0];
    way_table = &tables[1];
    rel_table = &tables[2];
}

middle_pgsql_t::~middle_pgsql_t() {
    for (int i=0; i < num_tables; i++) {
        if (tables[i].sql_conn) {
            PQfinish(tables[i].sql_conn);
        }
    }

}

boost::shared_ptr<const middle_query_t> middle_pgsql_t::get_instance() const {
    middle_pgsql_t* mid = new middle_pgsql_t();
    mid->out_options = out_options;
    mid->Append = out_options->append;

    //NOTE: this is thread safe for use in pending async processing only because
    //during that process they are only read from
    mid->cache = cache;
    // The persistent cache on the other hand is not thread-safe for reading,
    // so we create one per instance.
    if (out_options->flat_node_cache_enabled)
        mid->persistent_cache.reset(new node_persistent_cache(out_options,1,cache));

    // We use a connection per table to enable the use of COPY */
    for(int i=0; i<num_tables; i++) {
        //bomb if you cant connect
        if(mid->connect(mid->tables[i]))
            util::exit_nicely();
        PGconn* sql_conn = mid->tables[i].sql_conn;

        if (tables[i].prepare) {
            pgsql_exec(sql_conn, PGRES_COMMAND_OK, "%s", tables[i].prepare);
        }

        if (Append && tables[i].prepare_intarray) {
            pgsql_exec(sql_conn, PGRES_COMMAND_OK, "%s", tables[i].prepare_intarray);
        }
    }

    return boost::shared_ptr<const middle_query_t>(mid);
}

size_t middle_pgsql_t::pending_count() const {
    return ways_pending_tracker->size() + rels_pending_tracker->size();
}
