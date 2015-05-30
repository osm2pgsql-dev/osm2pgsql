/* Implements the mid-layer processing for osm2pgsql
 * using several PostgreSQL tables
 *
 * This layer stores data read in from the planet.osm file
 * and is then read by the backend processing code to
 * emit the final geometry-enabled output formats
*/

#include "config.h"

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
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <boost/format.hpp>
#include <boost/foreach.hpp>
#include <boost/unordered_map.hpp>

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
char *pgsql_store_nodes(const idlist_t &nds) {
  static char *buffer;
  static size_t buflen;

  if( buflen <= nds.size() * 10 )
  {
    buflen = ((nds.size() * 10) | 4095) + 1;  // Round up to next page */
    buffer = (char *)realloc( buffer, buflen );
  }
_restart:

  char *ptr = buffer;
  bool first = true;
  *ptr++ = '{';
  for (idlist_t::const_iterator it = nds.begin(); it != nds.end(); ++it)
  {
    if (!first)
      *ptr++ = ',';
    ptr += sprintf(ptr, "%" PRIdOSMID, *it);

    if( (size_t) (ptr-buffer) > (buflen-20) ) // Almost overflowed? */
    {
      buflen <<= 1;
      buffer = (char *)realloc( buffer, buflen );

      goto _restart;
    }
    first = false;
  }

  *ptr++ = '}';
  *ptr++ = 0;

  return buffer;
}

// Special escape routine for escaping strings in array constants: double quote, backslash,newline, tab*/
inline char *escape_tag( char *ptr, const std::string &in, bool escape )
{
  BOOST_FOREACH(const char c, in)
  {
    switch(c)
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
        *ptr++ = c;
        break;
    }
  }
  return ptr;
}

// escape means we return '\N' for copy mode, otherwise we return just NULL */
const char *pgsql_store_tags(const taglist_t &tags, bool escape)
{
  static char *buffer;
  static int buflen;

  int countlist = tags.size();
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

  char *ptr = buffer;
  bool first = true;
  *ptr++ = '{';

  for (taglist_t::const_iterator it = tags.begin(); it != tags.end(); ++it)
  {
    int maxlen = (it->key.length() + it->value.length()) * 4;
    if( (ptr+maxlen-buffer) > (buflen-20) ) // Almost overflowed? */
    {
      buflen <<= 1;
      buffer = (char *)realloc( buffer, buflen );

      goto _restart;
    }
    if( !first ) *ptr++ = ',';
    *ptr++ = '"';
    ptr = escape_tag(ptr, it->key, escape);
    *ptr++ = '"';
    *ptr++ = ',';
    *ptr++ = '"';
    ptr = escape_tag(ptr, it->value, escape);
    *ptr++ = '"';

    first = false;
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

void pgsql_parse_tags(const char *string, taglist_t &tags)
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
    tags.push_back(tag(key, val));
    if( *string == ',' )
      string++;
  }
}

// Parses an array of integers */
void pgsql_parse_nodes(const char *string, idlist_t &nds)
{
  if( *string++ != '{' )
    return;

  while( *string != '}' )
  {
    char *ptr;
    nds.push_back(strtoosmid( string, &ptr, 10 ));
    string = ptr;
    if( *string == ',' )
      string++;
  }
}

int pgsql_endCopy(middle_pgsql_t::table_desc *table)
{
    // Terminate any pending COPY */
    if (table->copyMode) {
        PGconn *sql_conn = table->sql_conn;
        int stop = PQputCopyEnd(sql_conn, NULL);
        if (stop != 1) {
            fprintf(stderr, "COPY_END for %s failed: %s\n", table->copy, PQerrorMessage(sql_conn));
            util::exit_nicely();
        }

        PGresult *res = PQgetResult(sql_conn);
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

int middle_pgsql_t::local_nodes_set(const osmid_t& id, const double& lat,
                                    const double& lon, const taglist_t &tags)
{
    if( node_table->copyMode )
    {
      const char *tag_buf = pgsql_store_tags(tags,1);
      int length = strlen(tag_buf) + 64;
      char *buffer = (char *)alloca( length );
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

    // Four params: id, lat, lon, tags */
    const char *paramValues[4];
    char *buffer = (char *)alloca(64);
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
int middle_pgsql_t::local_nodes_get_list(nodelist_t &out, const idlist_t nds) const
{
    assert(out.empty());

    char tmp[16];

    char *tmp2 = (char *)malloc(sizeof(char) * nds.size() * 16);
    if (tmp2 == NULL) return 0; //failed to allocate memory, return */


    // create a list of ids in tmp2 to query the database  */
    sprintf(tmp2, "{");
    int countDB = 0;
    for(idlist_t::const_iterator it = nds.begin(); it != nds.end(); ++it) {
        // Check cache first */
        osmNode loc;
        if (cache->get(&loc, *it) == 0) {
            out.push_back(loc);
            continue;
        }

        countDB++;
        // Mark nodes as needing to be fetched from the DB */
        out.push_back(osmNode());

        snprintf(tmp, sizeof(tmp), "%" PRIdOSMID ",", *it);
        strncat(tmp2, tmp, sizeof(char)*(nds.size()*16 - 2));
    }
    tmp2[strlen(tmp2) - 1] = '}'; // replace last , with } to complete list of ids*/

    if (countDB == 0) {
        free(tmp2);
        return nds.size(); // All ids where in cache, so nothing more to do */
    }

    pgsql_endCopy(node_table);

    PGconn *sql_conn = node_table->sql_conn;

    char const *paramValues[1];
    paramValues[0] = tmp2;
    PGresult *res = pgsql_execPrepared(sql_conn, "get_node_list", 1, paramValues, PGRES_TUPLES_OK);
    int countPG = PQntuples(res);

    //store the pg results in a hashmap and telling it how many we expect
    boost::unordered_map<osmid_t, osmNode> pg_nodes(countPG);

    for (int i = 0; i < countPG; i++) {
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

    PQclear(res);
    free(tmp2);

    // If some of the nodes in the way don't exist, the returning list has holes.
    // Merge the two lists removing any holes.
    unsigned wrtidx = 0;
    for (unsigned i = 0; i < nds.size(); ++i) {
        if (std::isnan(out[i].lat)) {
            boost::unordered_map<osmid_t, osmNode>::iterator found = pg_nodes.find(nds[i]);
            if(found != pg_nodes.end()) {
                out[wrtidx] = found->second;
                ++wrtidx;
            }
        } else {
            if (wrtidx < i)
                out[wrtidx] = out[i];
            ++wrtidx;
        }
    }
    out.resize(wrtidx);

    return wrtidx;
}


int middle_pgsql_t::nodes_set(osmid_t id, double lat, double lon, const taglist_t &tags) {
    cache->set( id, lat, lon, tags );

    return (out_options->flat_node_cache_enabled)
             ? persistent_cache->set(id, lat, lon)
             : local_nodes_set(id, lat, lon, tags);
}

int middle_pgsql_t::nodes_get_list(nodelist_t &out, const idlist_t nds) const
{
    return (out_options->flat_node_cache_enabled)
             ? persistent_cache->get_list(out, nds)
             : local_nodes_get_list(out, nds);
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
    if (!mark_pending)
        return 0;

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

int middle_pgsql_t::ways_set(osmid_t way_id, const idlist_t &nds, const taglist_t &tags)
{
    // Three params: id, nodes, tags */
    const char *paramValues[4];
    char *buffer;

    if( way_table->copyMode )
    {
      const char *tag_buf = pgsql_store_tags(tags,1);
      char *node_buf = pgsql_store_nodes(nds);
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
    paramValues[1] = pgsql_store_nodes(nds);
    paramValues[2] = pgsql_store_tags(tags,0);
    pgsql_execPrepared(way_table->sql_conn, "insert_way", 3, (const char * const *)paramValues, PGRES_COMMAND_OK);
    return 0;
}

int middle_pgsql_t::ways_get(osmid_t id, taglist_t &tags, nodelist_t &nodes) const
{
    char const *paramValues[1];
    PGconn *sql_conn = way_table->sql_conn;

    // Make sure we're out of copy mode */
    pgsql_endCopy( way_table );

    char tmp[16];
    snprintf(tmp, sizeof(tmp), "%" PRIdOSMID, id);
    paramValues[0] = tmp;

    PGresult *res = pgsql_execPrepared(sql_conn, "get_way", 1, paramValues, PGRES_TUPLES_OK);

    if (PQntuples(res) != 1) {
        PQclear(res);
        return 1;
    }

    pgsql_parse_tags( PQgetvalue(res, 0, 1), tags );

    size_t num_nodes = strtoul(PQgetvalue(res, 0, 2), NULL, 10);
    idlist_t list;
    pgsql_parse_nodes( PQgetvalue(res, 0, 0), list);
    if (num_nodes != list.size()) {
        fprintf(stderr, "parse_nodes problem for way %s: expected nodes %zu got %zu\n",
                tmp, num_nodes, list.size());
        util::exit_nicely();
    }
    PQclear(res);

    nodes_get_list(nodes, list);
    return 0;
}

int middle_pgsql_t::ways_get_list(const idlist_t &ids, idlist_t &way_ids,
                                  multitaglist_t &tags, multinodelist_t &nodes) const {
    if (ids.empty())
        return 0;

    char tmp[16];
    char *tmp2;
    char const *paramValues[1];

    tmp2 = (char *)malloc(sizeof(char)*ids.size()*16);
    if (tmp2 == NULL) return 0; //failed to allocate memory, return */

    // create a list of ids in tmp2 to query the database  */
    sprintf(tmp2, "{");
    for(idlist_t::const_iterator it = ids.begin(); it != ids.end(); ++it) {
        snprintf(tmp, sizeof(tmp), "%" PRIdOSMID ",", *it);
        strncat(tmp2,tmp, sizeof(char)*(ids.size()*16 - 2));
    }
    tmp2[strlen(tmp2) - 1] = '}'; // replace last , with } to complete list of ids*/

    pgsql_endCopy(way_table);

    PGconn *sql_conn = way_table->sql_conn;

    paramValues[0] = tmp2;
    PGresult *res = pgsql_execPrepared(sql_conn, "get_way_list", 1, paramValues, PGRES_TUPLES_OK);
    int countPG = PQntuples(res);

    idlist_t wayidspg;

    for (int i = 0; i < countPG; i++) {
        wayidspg.push_back(strtoosmid(PQgetvalue(res, i, 0), NULL, 10));
    }


    // Match the list of ways coming from postgres in a different order
    //   back to the list of ways given by the caller */
    for(idlist_t::const_iterator it = ids.begin(); it != ids.end(); ++it) {
        for (int j = 0; j < countPG; j++) {
            if (*it == wayidspg[j]) {
                way_ids.push_back(*it);
                tags.push_back(taglist_t());
                pgsql_parse_tags(PQgetvalue(res, j, 2), tags.back());

                size_t num_nodes = strtoul(PQgetvalue(res, j, 3), NULL, 10);
                idlist_t list;
                pgsql_parse_nodes( PQgetvalue(res, j, 1), list);
                if (num_nodes != list.size()) {
                    fprintf(stderr, "parse_nodes problem for way %s: expected nodes %zu got %zu\n",
                            tmp, num_nodes, list.size());
                    util::exit_nicely();
                }

                nodes.push_back(nodelist_t());
                nodes_get_list(nodes.back(), list);

                break;
            }
        }
    }

    assert(way_ids.size() <= ids.size());

    PQclear(res);
    free(tmp2);

    return way_ids.size();
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

int middle_pgsql_t::relations_set(osmid_t id, const memberlist_t &members, const taglist_t &tags)
{
    // Params: id, way_off, rel_off, parts, members, tags */
    const char *paramValues[6];
    char *buffer;
    taglist_t member_list;
    char buf[64];

    idlist_t all_parts, node_parts, way_parts, rel_parts;
    all_parts.reserve(members.size());
    node_parts.reserve(members.size());
    way_parts.reserve(members.size());
    rel_parts.reserve(members.size());

    for (memberlist_t::const_iterator it = members.begin(); it != members.end(); ++it) {
        char type = 0;
        switch (it->type)
        {
            case OSMTYPE_NODE:     node_parts.push_back(it->id); type = 'n'; break;
            case OSMTYPE_WAY:      way_parts.push_back(it->id); type = 'w'; break;
            case OSMTYPE_RELATION: rel_parts.push_back(it->id); type = 'r'; break;
            default:
                fprintf(stderr, "Internal error: Unknown member type %d\n", it->type);
                util::exit_nicely();
        }
        sprintf( buf, "%c%" PRIdOSMID, type, it->id );
        member_list.push_back(tag(buf, it->role));
    }

    all_parts.insert(all_parts.end(), node_parts.begin(), node_parts.end());
    all_parts.insert(all_parts.end(), way_parts.begin(), way_parts.end());
    all_parts.insert(all_parts.end(), rel_parts.begin(), rel_parts.end());

    if( rel_table->copyMode )
    {
      char *tag_buf = strdup(pgsql_store_tags(tags,1));
      const char *member_buf = pgsql_store_tags(member_list,1);
      char *parts_buf = pgsql_store_nodes(all_parts);
      int length = strlen(member_buf) + strlen(tag_buf) + strlen(parts_buf) + 64;
      buffer = (char *)alloca(length);
      if( snprintf( buffer, length, "%" PRIdOSMID "\t%zu\t%zu\t%s\t%s\t%s\n",
                    id, node_parts.size(), node_parts.size() + way_parts.size(),
                    parts_buf, member_buf, tag_buf ) > (length-10) )
      { fprintf( stderr, "buffer overflow relation id %" PRIdOSMID "\n", id); return 1; }
      free(tag_buf);
      pgsql_CopyData(__FUNCTION__, rel_table->sql_conn, buffer);
      return 0;
    }
    buffer = (char *)alloca(64);
    char *ptr = buffer;
    paramValues[0] = ptr;
    ptr += sprintf(ptr, "%" PRIdOSMID, id ) + 1;
    paramValues[1] = ptr;
    ptr += sprintf(ptr, "%zu", node_parts.size() ) + 1;
    paramValues[2] = ptr;
    sprintf( ptr, "%zu", node_parts.size() + way_parts.size() );
    paramValues[3] = pgsql_store_nodes(all_parts);
    paramValues[4] = pgsql_store_tags(member_list,0);
    if( paramValues[4] )
        paramValues[4] = strdup(paramValues[4]);
    paramValues[5] = pgsql_store_tags(tags,0);
    pgsql_execPrepared(rel_table->sql_conn, "insert_rel", 6, (const char * const *)paramValues, PGRES_COMMAND_OK);
    if( paramValues[4] )
        free((void *)paramValues[4]);
    return 0;
}

int middle_pgsql_t::relations_get(osmid_t id, memberlist_t &members, taglist_t &tags) const
{
    char tmp[16];
    char const *paramValues[1];
    PGconn *sql_conn = rel_table->sql_conn;
    taglist_t member_temp;

    // Make sure we're out of copy mode */
    pgsql_endCopy( rel_table );

    snprintf(tmp, sizeof(tmp), "%" PRIdOSMID, id);
    paramValues[0] = tmp;

    PGresult *res = pgsql_execPrepared(sql_conn, "get_rel", 1, paramValues, PGRES_TUPLES_OK);
    // Fields are: members, tags, member_count */

    if (PQntuples(res) != 1) {
        PQclear(res);
        return 1;
    }

    pgsql_parse_tags(PQgetvalue(res, 0, 1), tags);
    pgsql_parse_tags(PQgetvalue(res, 0, 0), member_temp);

    if (member_temp.size() != strtoul(PQgetvalue(res, 0, 2), NULL, 10)) {
        fprintf(stderr, "Unexpected member_count reading relation %" PRIdOSMID "\n", id);
        util::exit_nicely();
    }

    PQclear(res);

    for (taglist_t::const_iterator it = member_temp.begin(); it != member_temp.end(); ++it) {
        char tag = it->key[0];
        OsmType type = (tag == 'n')?OSMTYPE_NODE:(tag == 'w')?OSMTYPE_WAY:(tag == 'r')?OSMTYPE_RELATION:((OsmType)-1);
        members.push_back(member(type,
                                 strtoosmid(it->key.c_str()+1, NULL, 10 ),
                                 it->value));
    }
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

idlist_t middle_pgsql_t::relations_using_way(osmid_t way_id) const
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
    idlist_t rel_ids(ntuples);
    for (int i = 0; i < ntuples; ++i) {
        rel_ids[i] = strtoosmid(PQgetvalue(result, i, 0), NULL, 10);
    }
    PQclear(result);

    return rel_ids;
}

void middle_pgsql_t::analyze(void)
{
    for (int i=0; i<num_tables; i++) {
        PGconn *sql_conn = tables[i].sql_conn;

        if (tables[i].analyze) {
            pgsql_exec(sql_conn, PGRES_COMMAND_OK, "%s", tables[i].analyze);
        }
    }
}

void middle_pgsql_t::end(void)
{
    for (int i=0; i<num_tables; i++) {
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

    pgsql_exec_simple(sql_conn, PGRES_COMMAND_OK, (boost::format("SET search_path TO %1%,public;") % out_options->schema.get()).str());
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

    // Gazetter doesn't use mark-pending processing and consequently
    // needs no way-node index.
    // TODO Currently, set here to keep the impact on the code small.
    // We actually should have the output plugins report their needs
    // and pass that via the constructor to middle_t, so that middle_t
    // itself doesn't need to know about details of the output.
    if (out_options->output_backend == "gazetteer") {
        way_table->array_indexes = NULL;
        mark_pending = false;
    }

    append = out_options->append;
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

        if (append && tables[i].prepare_intarray) {
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
      append(0), mark_pending(true), cache(), persistent_cache(), build_indexes(0)
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
    mid->append = out_options->append;
    mid->mark_pending = mark_pending;

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

        if (append && tables[i].prepare_intarray) {
            pgsql_exec(sql_conn, PGRES_COMMAND_OK, "%s", tables[i].prepare_intarray);
        }
    }

    return boost::shared_ptr<const middle_query_t>(mid);
}

size_t middle_pgsql_t::pending_count() const {
    return ways_pending_tracker->size() + rels_pending_tracker->size();
}
