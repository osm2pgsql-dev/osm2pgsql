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
#include <boost/foreach.hpp>
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


// not thread safe, needs to be moved in the thread structure before
// middle tables can be _written_ in multi-threaded mode.
static boost::format single_fmt = boost::format("%1%");
static boost::format latlon_fmt = boost::format("%.10g");


middle_pgsql_t::table_desc::~table_desc() {
    if (sql_conn)
        PQfinish(sql_conn);
}

int middle_pgsql_t::table_desc::connect(const options_t *options) {
    set_prefix_and_tbls(options, &name);
    set_prefix_and_tbls(options, &start);
    set_prefix_and_tbls(options, &create);
    set_prefix_and_tbls(options, &create_index);
    set_prefix_and_tbls(options, &prepare);
    set_prefix_and_tbls(options, &prepare_intarray);
    set_prefix_and_tbls(options, &copy);
    set_prefix_and_tbls(options, &analyze);
    set_prefix_and_tbls(options, &stop);
    set_prefix_and_tbls(options, &array_indexes);

    std::cerr << "Setting up table: " << name << "\n";
    sql_conn = PQconnectdb(options->conninfo.c_str());

    // Check to see that the backend connection was successfully made */
    if (PQstatus(sql_conn) != CONNECTION_OK) {
        std::cerr << "Connection to database failed: " << PQerrorMessage(sql_conn) << "\n";
        return 1;
    }

    return 0;
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
void middle_pgsql_t::table_desc::set_prefix_and_tbls(const options_t *options, std::string *str)
{
    size_t lastpos = 0;
    size_t startpos = str->find_first_of("{%", lastpos);

    while (startpos != std::string::npos) {
        size_t pfind = startpos;
        if ((*str)[startpos] == '{') {
            lastpos = str->find('}', startpos + 1);
            assert (lastpos != std::string::npos);

            pfind = str->find('%', startpos + 1);
        } else
            lastpos = pfind + 1;
        assert (pfind < str->length() - 1);
        std::string replacement;
        if (pfind < lastpos) {
            switch ((*str)[pfind + 1]) {
                case 'p': replacement = options->prefix; break;
                case 't':
                    if (options->tblsslim_data)
                        replacement = *(options->tblsslim_data);
                    break;
                case 'i':
                    if (options->tblsslim_index)
                        replacement = *(options->tblsslim_index);
                    break;
                case 'm': replacement = "UNLOGGED"; break;
            default: break; //nothing
            }
        }
        if ((*str)[startpos] == '{') {
            if (replacement.empty()) {
                str->erase(startpos, lastpos - startpos + 1);
                lastpos = startpos;
            } else {
                str->erase(lastpos,1);
                str->replace(pfind, 2, replacement);
                str->erase(startpos, 1);
                lastpos += replacement.length() - 4;
            }
        } else {
            str->replace(pfind, 2, replacement);
            lastpos += replacement.length() - 2;
        }

        startpos = str->find_first_of("{%", lastpos);
    }
}


#define HELPER_STATE_UNINITIALIZED -1
#define HELPER_STATE_FORKED -2
#define HELPER_STATE_RUNNING 0
#define HELPER_STATE_FINISHED 1
#define HELPER_STATE_CONNECTED 2
#define HELPER_STATE_FAILED 3

namespace {
void pgsql_store_nodes(std::string &buffer, const osmid_t *nds, const int& nd_count)
{
  buffer.reserve(buffer.length() + (nd_count * 10) + 2);

  bool first = true;
  buffer += '{';
  for(int i = 0; i < nd_count; ++i)
  {
    if (!first) buffer += ',';
    buffer.append((single_fmt % nds[i]).str());
    first = false;
  }

  buffer += '}';
}

// Special escape routine for escaping strings in array constants: double quote, backslash,newline, tab*/
inline void escape_tag( std::string *ptr, const std::string &in, int escape )
{
  BOOST_FOREACH(const char c, in)
  {
    switch(c)
    {
      case '"':
        if( escape ) *ptr += '\\';
        *ptr += "\\\"";
        break;
      case '\\':
        if( escape ) *ptr += '\\';
        if( escape ) *ptr += '\\';
        *ptr += "\\\\";
        break;
      case '\n':
        if( escape ) *ptr += '\\';
        *ptr += "\\n";
        break;
      case '\r':
        if( escape ) *ptr += '\\';
        *ptr += "\\r";
        break;
      case '\t':
        if( escape ) *ptr += '\\';
        *ptr += "\\t";
        break;
      default:
        *ptr += c;
        break;
    }
  }
}

// escape means we return '\N' for copy mode, otherwise we return just NULL */
const char *pgsql_store_tags(std::string &buffer, const struct keyval *tags, int escape)
{
  if (!tags->listHasData())
  {
    if (escape)
      buffer += "\\N";

    return NULL;
  }

  bool first = true;
  buffer += '{';

  for (keyval* i = tags->firstItem(); i; i = tags->nextItem(i))
  {
    if (!first) buffer += ',';
    buffer += '"';
    escape_tag( &buffer, i->key, escape );
    buffer += "\",\"";
    escape_tag( &buffer, i->value, escape );
    buffer += '"';

    first = false;
  }

  buffer += '}';

  return buffer.c_str();
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
            std::cerr << "COPY_END for " << table->copy << " failed: " << PQerrorMessage(sql_conn) << "\n";
            util::exit_nicely();
        }

        res = PQgetResult(sql_conn);
        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            std::cerr << "COPY_END for " << table->copy << " failed: " << PQerrorMessage(sql_conn) << "\n";
            PQclear(res);
            util::exit_nicely();
        }
        PQclear(res);
        table->copyMode = 0;
    }
    return 0;
}
} // anonymous namespace

int middle_pgsql_t::local_nodes_set(osmid_t id, double lat, double lon, const struct keyval *tags)
{
    if (node_table->copyMode) {
      buffer.clear();
      // id
      buffer.append((single_fmt % id).str());
      buffer += '\t';

      // lat, lon
#ifdef FIXED_POINT
      buffer.append((single_fmt % util::double_to_fix(lat, out_options->scale)).str());
      buffer += '\t';
      buffer.append((single_fmt % util::double_to_fix(lon, out_options->scale)).str());
      buffer += '\t';
#else
      buffer.append((latlon_fmt % lat).str());
      buffer += '\t';
      buffer.append((latlon_fmt % lon).str());
      buffer += '\t';
#endif

      // tags
      pgsql_store_tags(buffer, tags, 1);
      buffer += '\n';

      pgsql_CopyData(__FUNCTION__, node_table->sql_conn, buffer);
    } else {
        std::string idstr = (single_fmt % id).str();
    #ifdef FIXED_POINT
        std::string latstr = (single_fmt % util::double_to_fix(lat, out_options->scale)).str();
        std::string lonstr = (single_fmt % util::double_to_fix(lon, out_options->scale)).str();
    #else
        std::string latstr = (latlon_fmt % lat).str();
        std::string lonstr = (latlon_fmt % lon).str();
    #endif
        std::string tagstr;
        // Four params: id, lat, lon, tags */
        const char *paramValues[] = { idstr.c_str(), latstr.c_str(), lonstr.c_str(),
                                      pgsql_store_tags(tagstr, tags, 0) };
        pgsql_execPrepared(node_table->sql_conn, "insert_node", 4, paramValues, PGRES_COMMAND_OK);
    }

    return 0;
}

// This should be made more efficient by using an IN(ARRAY[]) construct */
int middle_pgsql_t::local_nodes_get_list(struct osmNode *nodes, const osmid_t *ndids, const int& nd_count) const
{
    int count,  countDB, countPG, i,j;
    char const *paramValues[1];

    PGresult *res;
    PGconn *sql_conn = node_table->sql_conn;

    count = 0; countDB = 0;

    // create a list of ids in tmp2 to query the database  */
    std::string tmp2;
    tmp2.reserve(nd_count*16);
    tmp2 += '{';
    char tmp[22];
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
        tmp2.append(tmp);
    }

    if (countDB == 0) {
        return count; // All ids where in cache, so nothing more to do */
    }

    tmp2[tmp2.length() - 1] = '}'; // replace last , with } to complete list of ids*/

    pgsql_endCopy(node_table);

    paramValues[0] = tmp2.c_str();
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

    if( way_table->copyMode )
    {
      buffer.clear();
      // id
      buffer.append((single_fmt % way_id).str());
      buffer += '\t';
      // nodes
      pgsql_store_nodes(buffer, nds, nd_count);
      buffer += '\t';
      // tags
      pgsql_store_tags(buffer, tags, 1);
      buffer += '\n';

      pgsql_CopyData(__FUNCTION__, way_table->sql_conn, buffer);
    } else {
      // Three params: id, nodes, tags */
      std::string idstr = (single_fmt % way_id).str();
      std::string nodestr;
      pgsql_store_nodes(nodestr, nds, nd_count);
      std::string tagstr;
      const char *paramValues[] = { idstr.c_str(),
                                    nodestr.c_str(),
                                    pgsql_store_tags(tagstr, tags, 0) };
      pgsql_execPrepared(way_table->sql_conn, "insert_way", 3, paramValues, PGRES_COMMAND_OK);
    }

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

    int count, countPG, i, j;
    osmid_t *wayidspg;
    char const *paramValues[1];
    int num_nodes;
    osmid_t *list;

    PGresult *res;
    PGconn *sql_conn = way_table->sql_conn;

    if (way_count == 0) return 0;

    std::string tmp2;
    tmp2.reserve(way_count*16);

    // create a list of ids in tmp2 to query the database  */
    tmp2 += '{';
    char tmp[22];
    for( i=0; i<way_count; i++ ) {
        snprintf(tmp, sizeof(tmp), "%" PRIdOSMID ",", ids[i]);
        tmp2.append(tmp);
    }

    tmp2[tmp2.length() - 1] = '}'; // replace last , with } to complete list of ids*/

    pgsql_endCopy(way_table);

    paramValues[0] = tmp2.c_str();
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
      buffer.clear();
      // id
      buffer.append((single_fmt % id).str());
      buffer += '\t';
      // way_off
      buffer.append((single_fmt % node_count).str());
      buffer += '\t';
      // rel_off
      buffer.append((single_fmt % (node_count + way_count)).str());
      buffer += '\t';
      // parts
      pgsql_store_nodes(buffer, &all_parts[0], all_count);
      buffer += '\t';
      // members
      pgsql_store_tags(buffer, &member_list, 1);
      buffer += '\t';
      // tags
      pgsql_store_tags(buffer, tags, 1);
      buffer += '\n';

      pgsql_CopyData(__FUNCTION__, rel_table->sql_conn, buffer);
    } else {
      std::string idstr = (single_fmt % id).str();
      std::string wstr = (single_fmt % node_count).str();
      std::string rstr = (single_fmt % (node_count + way_count)).str();
      std::string partstr;
      pgsql_store_nodes(partstr, &all_parts[0], all_count);
      std::string memberstr;
      std::string tagstr;
      // Params: id, way_off, rel_off, parts, members, tags */
      const char *paramValues[] = { idstr.c_str(), wstr.c_str(), rstr.c_str(),
                                    partstr.c_str(),
                                    pgsql_store_tags(memberstr, &member_list, 0),
                                    pgsql_store_tags(tagstr, tags, 0)
                                  };
      pgsql_execPrepared(rel_table->sql_conn, "insert_rel", 6, paramValues, PGRES_COMMAND_OK);
    }

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

        if (!tables[i].analyze.empty()) {
            pgsql_exec(sql_conn, PGRES_COMMAND_OK, "%s", tables[i].analyze.c_str());
        }
    }
}

void middle_pgsql_t::end(void)
{
    int i;

    for (i=0; i<num_tables; i++) {
        PGconn *sql_conn = tables[i].sql_conn;

        // Commit transaction */
        if (!tables[i].stop.empty() && tables[i].transactionMode) {
            pgsql_exec(sql_conn, PGRES_COMMAND_OK, "%s", tables[i].stop.c_str());
            tables[i].transactionMode = 0;
        }

    }
}



int middle_pgsql_t::start(const options_t *out_options_)
{
    out_options = out_options_;
    PGresult   *res;
    int i;
    int dropcreate = !out_options->append;

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
        if(tables[i].connect(out_options))
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
                std::string sql;
                sql.reserve(24 + tables[t_node].name.length());
                sql += "SELECT id FROM ";
                sql += tables[t_node].name;
                sql += " LIMIT 1";
                res = PQexec(sql_conn, sql.c_str());
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
            pgsql_exec(sql_conn, PGRES_COMMAND_OK, "DROP TABLE IF EXISTS %s", tables[i].name.c_str());
        }

        if (!tables[i].start.empty()) {
            pgsql_exec(sql_conn, PGRES_COMMAND_OK, "%s", tables[i].start.c_str());
            tables[i].transactionMode = 1;
        }

        if (dropcreate && !tables[i].create.empty()) {
            pgsql_exec(sql_conn, PGRES_COMMAND_OK, "%s", tables[i].create.c_str());
            if (!tables[i].create_index.empty()) {
              pgsql_exec(sql_conn, PGRES_COMMAND_OK, "%s", tables[i].create_index.c_str());
            }
        }
        pgsql_exec(sql_conn, PGRES_COMMAND_OK, "RESET client_min_messages");

        if (!tables[i].prepare.empty()) {
            pgsql_exec(sql_conn, PGRES_COMMAND_OK, "%s", tables[i].prepare.c_str());
        }

        if (Append && !tables[i].prepare_intarray.empty()) {
            pgsql_exec(sql_conn, PGRES_COMMAND_OK, "%s", tables[i].prepare_intarray.c_str());
        }

        if (!tables[i].copy.empty()) {
            pgsql_exec(sql_conn, PGRES_COPY_IN, "%s", tables[i].copy.c_str());
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
        if (!tables[i].stop.empty() && tables[i].transactionMode) {
            pgsql_exec(sql_conn, PGRES_COMMAND_OK, "%s", tables[i].stop.c_str());
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

    std::cerr << "Stopping table: " << table->name << "\n";
    pgsql_endCopy(table);
    time(&start);
    if (out_options->droptemp)
    {
        pgsql_exec(sql_conn, PGRES_COMMAND_OK, "DROP TABLE %s", table->name.c_str());
    }
    else if (build_indexes && !table->array_indexes.empty())
    {
        std::cerr << "Building index on table: " << table->name << "\n";
        pgsql_exec(sql_conn, PGRES_COMMAND_OK, "%s", table->array_indexes.c_str());
    }

    PQfinish(sql_conn);
    table->sql_conn = NULL;
    time(&end);
    std::cerr << "Stopped table: " << table->name << " in " << (int)(end - start) << "s\n";
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
};
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
    buffer.reserve(8 * 1024);
    /*table = t_node,*/
    tables.push_back(table_desc(
            /*name*/ "%p_nodes",
           /*start*/ "BEGIN;\n",
#ifdef FIXED_POINT
          /*create*/ "CREATE %m TABLE %p_nodes (id " POSTGRES_OSMID_TYPE " PRIMARY KEY {USING INDEX TABLESPACE %i}, lat int4 not null, lon int4 not null, tags text[]) {TABLESPACE %t};\n",
    /*create_index*/ "",
         /*prepare*/ "PREPARE insert_node (" POSTGRES_OSMID_TYPE ", int4, int4, text[]) AS INSERT INTO %p_nodes VALUES ($1,$2,$3,$4);\n"
#else
          /*create*/ "CREATE %m TABLE %p_nodes (id " POSTGRES_OSMID_TYPE " PRIMARY KEY {USING INDEX TABLESPACE %i}, lat double precision not null, lon double precision not null, tags text[]) {TABLESPACE %t};\n",
    /*create_index*/ "",
         /*prepare*/ "PREPARE insert_node (" POSTGRES_OSMID_TYPE ", double precision, double precision, text[]) AS INSERT INTO %p_nodes VALUES ($1,$2,$3,$4);\n"
#endif
               "PREPARE get_node (" POSTGRES_OSMID_TYPE ") AS SELECT lat,lon,tags FROM %p_nodes WHERE id = $1 LIMIT 1;\n"
               "PREPARE get_node_list(" POSTGRES_OSMID_TYPE "[]) AS SELECT id, lat, lon FROM %p_nodes WHERE id = ANY($1::" POSTGRES_OSMID_TYPE "[]);\n"
               "PREPARE delete_node (" POSTGRES_OSMID_TYPE ") AS DELETE FROM %p_nodes WHERE id = $1;\n",
/*prepare_intarray*/ "",
            /*copy*/ "COPY %p_nodes FROM STDIN;\n",
         /*analyze*/ "ANALYZE %p_nodes;\n",
            /*stop*/ "COMMIT;\n"
                         ));
    tables.push_back(table_desc(
        /*table t_way,*/
            /*name*/ "%p_ways",
           /*start*/ "BEGIN;\n",
          /*create*/ "CREATE %m TABLE %p_ways (id " POSTGRES_OSMID_TYPE " PRIMARY KEY {USING INDEX TABLESPACE %i}, nodes " POSTGRES_OSMID_TYPE "[] not null, tags text[]) {TABLESPACE %t};\n",
    /*create_index*/ "",
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
    /*create_index*/ "",
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
        if(mid->tables[i].connect(mid->out_options))
            util::exit_nicely();
        PGconn* sql_conn = mid->tables[i].sql_conn;

        if (!tables[i].prepare.empty()) {
            pgsql_exec(sql_conn, PGRES_COMMAND_OK, "%s", tables[i].prepare.c_str());
        }

        if (Append && !tables[i].prepare_intarray.empty()) {
            pgsql_exec(sql_conn, PGRES_COMMAND_OK, "%s", tables[i].prepare_intarray.c_str());
        }
    }

    return boost::shared_ptr<const middle_query_t>(mid);
}

size_t middle_pgsql_t::pending_count() const {
    return ways_pending_tracker->size() + rels_pending_tracker->size();
}
