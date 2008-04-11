/* Implements the mid-layer processing for osm2pgsql
 * using several PostgreSQL tables
 * 
 * This layer stores data read in from the planet.osm file
 * and is then read by the backend processing code to
 * emit the final geometry-enabled output formats
*/
 
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <libpq-fe.h>

#include "osmtypes.h"
#include "middle.h"
#include "middle-pgsql.h"
#include "output-pgsql.h"
#include "pgsql.h"

enum table_id {
    t_node, t_way
} ;

struct table_desc {
    //enum table_id table;
    const char *name;
    const char *start;
    const char *create;
    const char *prepare;
    const char *copy;
    const char *analyze;
    const char *stop;

    int copyMode;    /* True if we are in copy mode */
};

static struct table_desc tables [] = {
    { 
        //table: t_node,
         name: "%s_nodes",
        start: "BEGIN;\n",
       create: "CREATE TABLE %s_nodes (id int4 PRIMARY KEY, lat double precision, lon double precision, tags text[]);\n",
      prepare: "PREPARE insert_node (int4, double precision, double precision, text[]) AS INSERT INTO %s_nodes VALUES ($1,$2,$3);\n"
               "PREPARE get_node (int4) AS SELECT lat,lon,tags FROM %s_nodes WHERE id = $1 LIMIT 1;\n",
         copy: "COPY %s_nodes FROM STDIN;\n",
      analyze: "ANALYZE %s_nodes;\n",
         stop: "COMMIT;\n"
    },
    { 
        //table: t_way,
         name: "%s_ways",
        start: "BEGIN;\n",
       create: "CREATE TABLE %s_ways (id int4 NOT NULL, nodes int4[] not null, tags text[] not null, pending boolean);\n"
               "CREATE INDEX %s_ways_idx ON %s_ways (id);\n",
      prepare: "PREPARE insert_way (int4, int4[], text[], boolean) AS INSERT INTO %s_ways VALUES ($1,$2,$3,$4);\n"
               "PREPARE get_way (int4) AS SELECT nodes, tags, array_upper(nodes,1) FROM %s_ways WHERE id = $1;\n"
               "PREPARE way_done(int4) AS UPDATE %s_ways SET pending = false WHERE id = $1;\n"
               "PREPARE pending_ways AS SELECT id FROM %s_ways WHERE pending;\n",
         copy: "COPY %s_ways FROM STDIN;\n",
      analyze: "ANALYZE %s_ways;\n",
         stop:  "COMMIT;\n"
    }
};

static int num_tables = sizeof(tables)/sizeof(tables[0]);
static PGconn **sql_conns;

static void pgsql_cleanup(void)
{
    int i;

    if (!sql_conns)
           return;

    for (i=0; i<num_tables; i++) {
        if (sql_conns[i]) {
            PQfinish(sql_conns[i]);
            sql_conns[i] = NULL;
        }
    }
}

char *pgsql_store_nodes(int *nds, int nd_count)
{
  static char *buffer;
  static int buflen;

  char *ptr;
  int i, first;
    
  if( buflen <= nd_count * 10 )
  {
    buflen = ((nd_count * 10) | 4095) + 1;  /* Round up to next page */
    buffer = realloc( buffer, buflen );
  }
_restart:

  ptr = buffer;
  first = 1;
  *ptr++ = '{';
  for( i=0; i<nd_count; i++ )
  {
    if( !first ) *ptr++ = ',';
    ptr += sprintf( ptr, "%d", nds[i] );
    
    if( (ptr-buffer) > (buflen-20) ) /* Almost overflowed? */
    {
      buflen <<= 1;
      buffer = realloc( buffer, buflen );
      
      goto _restart;
    }
    first = 0;
  }
  
  *ptr++ = '}';
  *ptr++ = 0;
  
  return buffer;
}

/* Special escape routine for escaping strings in array constants: double quote, backslash,newline, tab*/
static inline char *escape_tag( char *ptr, const char *in )
{
  while( *in )
  {
    switch(*in)
    {
      case '"':
        *ptr++ = '\\';
        *ptr++ = '\\';
        *ptr++ = '"';
        break;
      case '\\':
        *ptr++ = '\\';
        *ptr++ = '\\';
        *ptr++ = '\\';
        *ptr++ = '\\';
        break;
      case '\n':
        *ptr++ = '\\';
        *ptr++ = '\\';
        *ptr++ = 'n';
        break;
      case '\t':
        *ptr++ = '\\';
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

char *pgsql_store_tags(struct keyval *tags)
{
  static char *buffer;
  static int buflen;

  char *ptr;
  struct keyval *i;
  int first;
    
  int countlist = countList(tags);
  if( buflen <= countlist * 24 ) /* LE so 0 always matches */
  {
    buflen = ((countlist * 24) | 4095) + 1;  /* Round up to next page */
    buffer = realloc( buffer, buflen );
  }
_restart:

  ptr = buffer;
  first = 1;
  *ptr++ = '{';
  /* The lists are circular, exit when we reach the head again */
  for( i=tags->next; i->key; i = i->next )
  {
    int maxlen = (strlen(i->key) + strlen(i->value)) * 4;
    if( (ptr+maxlen-buffer) > (buflen-20) ) /* Almost overflowed? */
    {
      buflen <<= 1;
      buffer = realloc( buffer, buflen );
      
      goto _restart;
    }
    if( !first ) *ptr++ = ',';
    *ptr++ = '"';
    ptr = escape_tag( ptr, i->key );
    *ptr++ = '"';
    *ptr++ = ',';
    *ptr++ = '"';
    ptr = escape_tag( ptr, i->value );
    *ptr++ = '"';
    
    first=0;
  }
  
  *ptr++ = '}';
  *ptr++ = 0;
  
  return buffer;
}

/* Decodes a portion of an array literal from postgres */
/* Argument should point to beginning of literal, on return points to delimiter */
static const char *decode_upto( const char *src, char *dst )
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

static void pgsql_parse_tags( const char *string, struct keyval *tags )
{
  char key[1024];
  char val[1024];
  
//  fprintf( stderr, "Parsing: %s\n", string );
  if( *string++ != '{' )
    return;
  while( *string != '}' )
  {
    string = decode_upto( string, key );
    /* String points to the comma */
    string++;
    string = decode_upto( string, val );
    /* String points to the comma or closing '}' */
    addItem( tags, key, val, 0 );
//    fprintf( stderr, "Extracted item: %s=%s\n", key, val );
    if( *string == ',' )
      string++;
  }
}

/* Parses an array of integers */
static void pgsql_parse_nodes( const char *src, int *nds, int nd_count )
{
  int count = 0;
  const char *string = src;
  
  if( *string++ != '{' )
    return;
  while( *string != '}' )
  {
    char *ptr;
    nds[count] = strtol( string, &ptr, 10 );
    string = ptr;
    if( *string == ',' )
      string++;
    count++;
  }
  if( count != nd_count )
  {
    fprintf( stderr, "parse_nodes problem: '%s' expected %d got %d\n", src, nd_count, count );
    exit_nicely();
  }
}

int pgsql_endCopy( enum table_id i )
{
    /* Terminate any pending COPY */
     if (tables[i].copyMode) {
        PGconn *sql_conn = sql_conns[i];
        int stop = PQputCopyEnd(sql_conn, NULL);
        if (stop != 1) {
            fprintf(stderr, "COPY_END for %s failed: %s\n", tables[i].copy, PQerrorMessage(sql_conn));
            exit_nicely();
        }

        PGresult *res = PQgetResult(sql_conn);
        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            fprintf(stderr, "COPY_END for %s failed: %s\n", tables[i].copy, PQerrorMessage(sql_conn));
            PQclear(res);
            exit_nicely();
        }
        PQclear(res);
        if (tables[i].analyze) {
            pgsql_exec(sql_conn, tables[i].analyze, PGRES_COMMAND_OK);
        }
        tables[i].copyMode = 0;
    }
    return 0;
}

static int pgsql_nodes_set(int id, double lat, double lon, struct keyval *tags)
{
    /* Four params: id, lat, lon, tags */
    char *paramValues[4];
    char *buffer;

    if( tables[t_node].copyMode )
    {
      char *tag_buf = pgsql_store_tags(tags);
      int length = strlen(tag_buf) + 64;
      buffer = alloca( length );
      
      if( snprintf( buffer, length, "%d\t%.10f\t%.10f\t%s\n", id, lat, lon, pgsql_store_tags(tags) ) > (length-10) )
      { fprintf( stderr, "buffer overflow node id %d\n", id); return 1; }
      return pgsql_CopyData(__FUNCTION__, sql_conns[t_node], buffer);
    }
    buffer = alloca(64);
    paramValues[0] = buffer;
    paramValues[1] = paramValues[0] + sprintf( paramValues[0], "%d", id ) + 1;
    paramValues[2] = paramValues[1] + sprintf( paramValues[1], "%.10f", lat ) + 1;
    sprintf( paramValues[2], "%.10f", lon );

    paramValues[3] = pgsql_store_tags(tags);
    pgsql_execPrepared(sql_conns[t_node], "insert_node", 4, (const char * const *)paramValues, PGRES_COMMAND_OK);
    return 0;
}


static int pgsql_nodes_get(struct osmNode *out, int id)
{
    PGresult   *res;
    char tmp[16];
    char const *paramValues[1];
    PGconn *sql_conn = sql_conns[t_node];

    /* Make sure we're out of copy mode */
    pgsql_endCopy( t_node );

    snprintf(tmp, sizeof(tmp), "%d", id);
    paramValues[0] = tmp;
 
    res = pgsql_execPrepared(sql_conn, "get_node", 1, paramValues, PGRES_TUPLES_OK);

    if (PQntuples(res) != 1) {
        PQclear(res);
        return 1;
    } 

    out->lat = strtod(PQgetvalue(res, 0, 0), NULL);
    out->lon = strtod(PQgetvalue(res, 0, 1), NULL);
    PQclear(res);
    return 0;
}

/* This should be made more efficient by using an IN(ARRAY[]) construct */
static int pgsql_nodes_get_list(struct osmNode *nodes, int *ndids, int nd_count)
{
    int count = 0, i;
    for( i=0; i<nd_count; i++ )
    {
      if( pgsql_nodes_get( &nodes[count], ndids[i] ) == 0 )
        count++;
    }
    return count;
}

static int pgsql_ways_set(int way_id, int *nds, int nd_count, struct keyval *tags, int pending)
{
    /* Three params: id, nodes, tags, pending */
    char *paramValues[4];
    char *buffer;

    if( tables[t_way].copyMode )
    {
      char *tag_buf = pgsql_store_tags(tags);
      char *node_buf = pgsql_store_nodes(nds, nd_count);
      int length = strlen(tag_buf) + strlen(node_buf) + 64;
      buffer = alloca(length);
      if( snprintf( buffer, length, "%d\t%s\t%s\t%c\n", 
              way_id, pgsql_store_nodes(nds, nd_count), pgsql_store_tags(tags), pending?'t':'f' ) > (length-10) )
      { fprintf( stderr, "buffer overflow way id %d\n", way_id); return 1; }
      return pgsql_CopyData(__FUNCTION__, sql_conns[t_way], buffer);
    }
    buffer = alloca(64);
    paramValues[0] = buffer;
    paramValues[3] = paramValues[0] + sprintf( paramValues[0], "%d", way_id ) + 1;
    sprintf( paramValues[3], "%c", pending?'t':'f' );
    paramValues[1] = pgsql_store_nodes(nds, nd_count);
    paramValues[2] = pgsql_store_tags(tags);
    pgsql_execPrepared(sql_conns[t_way], "insert_way", 4, (const char * const *)paramValues, PGRES_COMMAND_OK);
    return 0;
}

/* Caller is responsible for freeing nodesptr & resetList(tags) */
static int pgsql_ways_get(int id, struct keyval *tags, struct osmNode **nodes_ptr, int *count_ptr)
{
    PGresult   *res;
    char tmp[16];
    char const *paramValues[1];
    PGconn *sql_conn = sql_conns[t_way];

    /* Make sure we're out of copy mode */
    pgsql_endCopy( t_way );

    snprintf(tmp, sizeof(tmp), "%d", id);
    paramValues[0] = tmp;
 
    res = pgsql_execPrepared(sql_conn, "get_way", 1, paramValues, PGRES_TUPLES_OK);

    if (PQntuples(res) != 1) {
        PQclear(res);
        return 1;
    } 

    pgsql_parse_tags( PQgetvalue(res, 0, 1), tags );

    int num_nodes = strtol(PQgetvalue(res, 0, 2), NULL, 10);
    int *list = alloca( sizeof(int)*num_nodes );
    *nodes_ptr = malloc( sizeof(struct osmNode) * num_nodes );
    pgsql_parse_nodes( PQgetvalue(res, 0, 0), list, num_nodes);
    
    *count_ptr = pgsql_nodes_get_list( *nodes_ptr, list, num_nodes);
    PQclear(res);
    return 0;
}

static int pgsql_ways_done(int id)
{
    char tmp[16];
    char const *paramValues[1];
    PGconn *sql_conn = sql_conns[t_way];

    /* Make sure we're out of copy mode */
    pgsql_endCopy( t_way );

    snprintf(tmp, sizeof(tmp), "%d", id);
    paramValues[0] = tmp;
 
    pgsql_execPrepared(sql_conn, "way_done", 1, paramValues, PGRES_COMMAND_OK);

    return 0;
}

static void pgsql_iterate_ways(int (*callback)(int id, struct keyval *tags, struct osmNode *nodes, int count))
{
    PGresult   *res_ways;
    int i, count = 0;

    fprintf(stderr, "\nRetrieving way list\n");

    res_ways = pgsql_execPrepared(sql_conns[t_way], "pending_ways", 0, NULL, PGRES_TUPLES_OK);

    //fprintf(stderr, "\nIterating ways\n");
    for (i = 0; i < PQntuples(res_ways); i++) {
        int id = strtol(PQgetvalue(res_ways, i, 0), NULL, 10);
        struct keyval tags;
        struct osmNode *nodes;
        int nd_count;

        if (count++ %1000 == 0)
                fprintf(stderr, "\rprocessing way (%dk)", count/1000);

        initList(&tags);
        if( pgsql_ways_get(id, &tags, &nodes, &nd_count) )
          continue;
          
        callback(id, &tags, nodes, nd_count);

        free(nodes);
        resetList(&tags);
    }

    PQclear(res_ways);
    fprintf(stderr, "\n");
}

static void pgsql_analyze(void)
{
    int i;

    for (i=0; i<num_tables; i++) {
        PGconn *sql_conn = sql_conns[i];
 
        if (tables[i].analyze) {
            pgsql_exec(sql_conn, tables[i].analyze, PGRES_COMMAND_OK );
        }
    }
}

static void pgsql_end(void)
{
    int i;

    for (i=0; i<num_tables; i++) {
        PGconn *sql_conn = sql_conns[i];
 

        // Commit transaction
        if (tables[i].stop) {
            pgsql_exec(sql_conn, tables[i].stop, PGRES_COMMAND_OK);
        }

    }
}

/* Replace %s with prefix */
static inline void set_prefix( const char *prefix, const char **string )
{
  char buffer[1024];
  sprintf( buffer, *string, prefix, prefix, prefix, prefix );
  *string = strdup( buffer );
}

static int pgsql_start(const struct output_options *options)
{
    char sql[2048];
    PGresult   *res;
    int i;
    int dropcreate = 1;

    /* We use a connection per table to enable the use of COPY */
    sql_conns = calloc(num_tables, sizeof(PGconn *));
    assert(sql_conns);

    for (i=0; i<num_tables; i++) {
        PGconn *sql_conn;
                        
        set_prefix( options->prefix, &(tables[i].name) );
        set_prefix( options->prefix, &(tables[i].start) );
        set_prefix( options->prefix, &(tables[i].create) );
        set_prefix( options->prefix, &(tables[i].prepare) );
        set_prefix( options->prefix, &(tables[i].copy) );
        set_prefix( options->prefix, &(tables[i].analyze) );
        set_prefix( options->prefix, &(tables[i].stop) );

        fprintf(stderr, "Setting up table: %s\n", tables[i].name);
        sql_conn = PQconnectdb(options->conninfo);

        /* Check to see that the backend connection was successfully made */
        if (PQstatus(sql_conn) != CONNECTION_OK) {
            fprintf(stderr, "Connection to database failed: %s\n", PQerrorMessage(sql_conn));
            exit_nicely();
        }
        sql_conns[i] = sql_conn;

        if (dropcreate) {
            sql[0] = '\0';
            strcat(sql, "DROP TABLE ");
            strcat(sql, tables[i].name);
            res = PQexec(sql_conn, sql);
            PQclear(res); /* Will be an error if table does not exist */
        }

        if (tables[i].start) {
            pgsql_exec(sql_conn, tables[i].start, PGRES_COMMAND_OK);
        }

        if (dropcreate && tables[i].create) {
            pgsql_exec(sql_conn, tables[i].create, PGRES_COMMAND_OK);
        }

        if (tables[i].prepare) {
            pgsql_exec(sql_conn, tables[i].prepare, PGRES_COMMAND_OK);
        }

        if (tables[i].copy) {
            pgsql_exec(sql_conn, tables[i].copy, PGRES_COPY_IN);
            tables[i].copyMode = 1;
        }
    }

    return 0;
}

static void pgsql_stop(void)
{
    PGconn *sql_conn;
    int i;

   for (i=0; i<num_tables; i++) {
        //fprintf(stderr, "Stopping table: %s\n", tables[i].name);
        pgsql_endCopy(i);
        sql_conn = sql_conns[i];
        if (tables[i].stop) {
            pgsql_exec(sql_conn, tables[i].stop, PGRES_COMMAND_OK);
        }
        PQfinish(sql_conn);
        sql_conns[i] = NULL;
    }
    free(sql_conns);
    sql_conns = NULL;
}
 
struct middle_t mid_pgsql = {
        start:          pgsql_start,
        stop:           pgsql_stop,
        cleanup:        pgsql_cleanup,
        analyze:        pgsql_analyze,
        end:            pgsql_end,
        nodes_set:      pgsql_nodes_set,
//        nodes_get:      pgsql_nodes_get,
        nodes_get_list:      pgsql_nodes_get_list,
        ways_set:       pgsql_ways_set,
        ways_get:       pgsql_ways_get,
        ways_done:      pgsql_ways_done,
//        iterate_nodes:  pgsql_iterate_nodes,
        iterate_ways:   pgsql_iterate_ways
};
