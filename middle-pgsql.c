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

#ifdef HAVE_PTHREAD
#include <pthread.h>
#endif

#include <libpq-fe.h>

#include "osmtypes.h"
#include "middle.h"
#include "middle-pgsql.h"
#include "output-pgsql.h"
#include "pgsql.h"

/* Store +-20,000km Mercator co-ordinates as fixed point 32bit number with maximum precision */
/* Scale is chosen such that 40,000 * SCALE < 2^32          */
#define FIXED_POINT

static int scale = 100;
#define DOUBLE_TO_FIX(x) ((int)((x) * scale))
#define FIX_TO_DOUBLE(x) (((double)x) / scale)



enum table_id {
    t_node, t_way, t_rel
} ;

struct table_desc {
    //enum table_id table;
    const char *name;
    const char *start;
    const char *create;
    const char *create_index;
    const char *prepare;
    const char *prepare_intarray;
    const char *copy;
    const char *analyze;
    const char *stop;
    const char *array_indexes;

    int copyMode;    /* True if we are in copy mode */
    PGconn *sql_conn;
};

static struct table_desc tables [] = {
    { 
        //table = t_node,
         .name = "%p_nodes",
        .start = "BEGIN;\n",
#ifdef FIXED_POINT
       .create = "CREATE TABLE %p_nodes (id int4 PRIMARY KEY {USING INDEX TABLESPACE %i}, lat int4 not null, lon int4 not null, tags text[]) {TABLESPACE %t};\n",
      .prepare = "PREPARE insert_node (int4, int4, int4, text[]) AS INSERT INTO %p_nodes VALUES ($1,$2,$3,$4);\n"
#else
       .create = "CREATE TABLE %p_nodes (id int4 PRIMARY KEY {USING INDEX TABLESPACE %i}, lat double precision not null, lon double precision not null, tags text[]) {TABLESPACE %t};\n",
      .prepare = "PREPARE insert_node (int4, double precision, double precision, text[]) AS INSERT INTO %p_nodes VALUES ($1,$2,$3,$4);\n"
#endif
               "PREPARE get_node (int4) AS SELECT lat,lon,tags FROM %p_nodes WHERE id = $1 LIMIT 1;\n"
               "PREPARE delete_node (int4) AS DELETE FROM %p_nodes WHERE id = $1;\n",
.prepare_intarray = // This is to fetch lots of nodes simultaneously, in order including duplicates. The commented out version doesn't do duplicates
                  // It's not optimal as it does a Nested Loop / Index Scan which is suboptimal for large arrays
                  //"PREPARE get_node_list(int[]) AS SELECT id, lat, lon FROM %p_nodes WHERE id = ANY($1::int4[]) ORDER BY $1::int4[] # id\n",
		 //"PREPARE get_node_list(int[]) AS select y.id, y.lat, y.lon from (select i, ($1)[i] as l_id from (select generate_series(1,icount($1)) as i) x) z, "
		 //                                    "(select * from %p_nodes where id = ANY($1)) y where l_id=id order by i;\n",
		 "PREPARE get_node_list(int[]) AS SELECT id, lat, lon FROM %p_nodes WHERE id = ANY($1::int4[])",
         .copy = "COPY %p_nodes FROM STDIN;\n",
      .analyze = "ANALYZE %p_nodes;\n",
         .stop = "COMMIT;\n"
    },
    { 
        //table = t_way,
         .name = "%p_ways",
        .start = "BEGIN;\n",
       .create = "CREATE TABLE %p_ways (id int4 PRIMARY KEY {USING INDEX TABLESPACE %i}, nodes int4[] not null, tags text[], pending boolean not null) {TABLESPACE %t};\n",
 .create_index = "CREATE INDEX %p_ways_idx ON %p_ways (id) {TABLESPACE %i} WHERE pending;\n",
.array_indexes = "CREATE INDEX %p_ways_nodes ON %p_ways USING gin (nodes gin__int_ops) {TABLESPACE %i};\n",
      .prepare = "PREPARE insert_way (int4, int4[], text[], boolean) AS INSERT INTO %p_ways VALUES ($1,$2,$3,$4);\n"
               "PREPARE get_way (int4) AS SELECT nodes, tags, array_upper(nodes,1) FROM %p_ways WHERE id = $1;\n"
               "PREPARE way_done(int4) AS UPDATE %p_ways SET pending = false WHERE id = $1;\n"
               "PREPARE pending_ways AS SELECT id FROM %p_ways WHERE pending;\n"
               "PREPARE delete_way(int4) AS DELETE FROM %p_ways WHERE id = $1;\n",
.prepare_intarray = "PREPARE node_changed_mark(int4) AS UPDATE %p_ways SET pending = true WHERE nodes && ARRAY[$1] AND NOT pending;\n",
         .copy = "COPY %p_ways FROM STDIN;\n",
      .analyze = "ANALYZE %p_ways;\n",
         .stop =  "COMMIT;\n"
    },
    { 
        //table = t_rel,
         .name = "%p_rels",
        .start = "BEGIN;\n",
       .create = "CREATE TABLE %p_rels(id int4 PRIMARY KEY {USING INDEX TABLESPACE %i}, way_off int2, rel_off int2, parts int4[], members text[], tags text[], pending boolean not null) {TABLESPACE %t};\n",
 .create_index = "CREATE INDEX %p_rels_idx ON %p_rels (id) {TABLESPACE %i} WHERE pending;\n",
.array_indexes = "CREATE INDEX %p_rels_parts ON %p_rels USING gin (parts gin__int_ops) {TABLESPACE %i};\n",
      .prepare = "PREPARE insert_rel (int4, int2, int2, int[], text[], text[]) AS INSERT INTO %p_rels VALUES ($1,$2,$3,$4,$5,$6,false);\n"
               "PREPARE get_rel (int4) AS SELECT members, tags, array_upper(members,1)/2 FROM %p_rels WHERE id = $1;\n"
               "PREPARE rel_done(int4) AS UPDATE %p_rels SET pending = false WHERE id = $1;\n"
               "PREPARE pending_rels AS SELECT id FROM %p_rels WHERE pending;\n"
               "PREPARE delete_rel(int4) AS DELETE FROM %p_rels WHERE id = $1;\n",
.prepare_intarray = /* Note: don't use subarray here since (at least in 8.1) has odd effects if you request stuff out of range */
                "PREPARE node_changed_mark(int4) AS UPDATE %p_rels SET pending = true WHERE parts && ARRAY[$1] AND parts[1:way_off] && ARRAY[$1] AND NOT pending;\n"
                "PREPARE way_changed_mark(int4) AS UPDATE %p_rels SET pending = true WHERE parts && ARRAY[$1] AND parts[way_off+1:rel_off] && ARRAY[$1] AND NOT pending;\n"
                  /* For this it works fine */
                "PREPARE rel_changed_mark(int4) AS UPDATE %p_rels SET pending = true WHERE parts && ARRAY[$1] AND subarray(parts,rel_off+1) && ARRAY[$1] AND NOT pending;\n",
         .copy = "COPY %p_rels FROM STDIN;\n",
      .analyze = "ANALYZE %p_rels;\n",
         .stop =  "COMMIT;\n"
    }
};

static const int num_tables = sizeof(tables)/sizeof(tables[0]);
static int warn_node_order;
static struct table_desc *node_table = &tables[t_node];
static struct table_desc *way_table  = &tables[t_way];
static struct table_desc *rel_table  = &tables[t_rel];

/* Here we use a similar storage structure as middle-ram, except we allow
 * the array to be lossy so we can cap the total memory usage. Hence it is a
 * combination of a sparse array with a priority queue
 *
 * Like middle-ram we have a number of blocks all storing PER_BLOCK
 * ramNodes. However, here we also track the number of nodes in each block.
 * Seperately we have a priority queue like structure when maintains a list
 * of all the used block so we can easily find the block with the least
 * nodes. The cache has two phases:
 *
 * Phase 1: Loading initially, usedBlocks < maxBlocks. In this case when a
 * new block is needed we simply allocate it and put it in
 * queue[usedBlocks-1] which is the bottom of the tree. Every node added
 * increases it's usage. When we move onto the next block we percolate this
 * block up the queue until it reaches its correct position. The invariant
 * is that the priority tree is complete except for this last node. We do
 * not permit adding nodes to any other block to preserve this invariant.
 *
 * Phase 2: Once we've reached the maximum number of blocks permitted, we
 * change so that the block currently be inserted into is at the top of the
 * tree. When a new block is needed we take the one at the end of the queue,
 * as it is the one with the least number of nodes in it. When we move onto
 * the next block we first push the just completed block down to it's
 * correct position in the queue and then reuse the block that now at the
 * head.
 *
 * The result being that at any moment we have in memory the top maxBlock
 * blocks in terms of number of nodes in memory. This should maximize the
 * number of hits in lookups.
 *
 * Complexity:
 *  Insert node: O(1)
 *  Lookup node: O(1)
 *  Add new block: O(log usedBlocks)
 *  Reuse old block: O(log maxBlocks)
 */

struct ramNode {
#ifdef FIXED_POINT
    int lon;
    int lat;
#else
    double lon;
    double lat;
#endif
};

struct ramNodeBlock {
  struct ramNode    *nodes;
  int used;
};

#define BLOCK_SHIFT 10
#define PER_BLOCK  (1 << BLOCK_SHIFT)
#define NUM_BLOCKS (1 << (32 - BLOCK_SHIFT))

static struct ramNodeBlock blocks[NUM_BLOCKS];
static int usedBlocks;
/* Note: maxBlocks *must* be odd, to make sure the priority queue has no nodes with only one child */
static int maxBlocks = 0;
static struct ramNodeBlock **queue;
static int storedNodes, totalNodes;
int nodesCacheHits, nodesCacheLookups;

static int Append;

static inline int id2block(int id)
{
    // + NUM_BLOCKS/2 allows for negative IDs
    return (id >> BLOCK_SHIFT) + NUM_BLOCKS/2;
}

static inline int id2offset(int id)
{
    return id & (PER_BLOCK-1);
}

static inline int block2id(int block, int offset)
{
    return ((block - NUM_BLOCKS/2) << BLOCK_SHIFT) + offset;
}

#define Swap(a,b) { typeof(a) __tmp = a; a = b; b = __tmp; }
static void percolate_up( int pos )
{
    int i = pos;
    while( i > 0 )
    {
      int parent = (i-1)>>1;
      if( queue[i]->used < queue[parent]->used )
      {
        Swap( queue[i], queue[parent] );
        i = parent;
      }
      else
        break;
    }
}

#define UNUSED  __attribute__ ((unused))
static int pgsql_ram_nodes_set(int id, double lat, double lon, struct keyval *tags UNUSED)
{
    int block  = id2block(id);
    int offset = id2offset(id);
    
    totalNodes++;

    if (!blocks[block].nodes) {
        if( usedBlocks < maxBlocks )
        {
          /* We've just finished with the previous block, so we need to percolate it up the queue to its correct position */
          if( usedBlocks > 0 )
            /* Upto log(usedBlocks) iterations */
            percolate_up( usedBlocks-1 );

          blocks[block].nodes = calloc(PER_BLOCK, sizeof(struct ramNode));
          blocks[block].used = 0;
          if (!blocks[block].nodes) {
              fprintf(stderr, "Error allocating nodes\n");
              exit_nicely();
          }
          queue[usedBlocks] = &blocks[block];
          usedBlocks++;

          /* If we've just used up the last possible block we enter the
           * transition and we change the invariant. To do this we percolate
           * the newly allocated block straight to the head */
          if( usedBlocks == maxBlocks )
            percolate_up( usedBlocks-1 );
        }
        else
        {
          /* We've reached the maximum number of blocks, so now we push the
           * current head of the tree down to the right level to restore the
           * priority queue invariant. Upto log(maxBlocks) iterations */
          
          int i=0;
          while( 2*i+1 < maxBlocks )
          {
            if( queue[2*i+1]->used <= queue[2*i+2]->used )
            {
              if( queue[i]->used > queue[2*i+1]->used )
              {
                Swap( queue[i], queue[2*i+1] );
                i = 2*i+1;
              }
              else
                break;
            }
            else
            {
              if( queue[i]->used > queue[2*i+2]->used )
              {
                Swap( queue[i], queue[2*i+2] );
                i = 2*i+2;
              }
              else
                break;
            }
          }
          /* Now the head of the queue is the smallest, so it becomes our replacement candidate */
          blocks[block].nodes = queue[0]->nodes;
          blocks[block].used = 0;
          memset( blocks[block].nodes, 0, PER_BLOCK * sizeof(struct ramNode) );
          
          /* Clear old head block and point to new block */
          storedNodes -= queue[0]->used;
          queue[0]->nodes = NULL;
          queue[0]->used = 0;
          queue[0] = &blocks[block];
        }
    }
    else
    {
      /* Insert into an existing block. We can't allow this in general or it
       * will break the invariant. However, it will work fine if all the
       * nodes come in numerical order, which is the common case */
      
      int expectedpos;
      if( usedBlocks < maxBlocks )
        expectedpos = usedBlocks-1;
      else
        expectedpos = 0;
        
      if( queue[expectedpos] != &blocks[block] )
      {
        if (!warn_node_order) {
            fprintf( stderr, "WARNING: Found Out of order node %d (%d,%d) - this will impact the cache efficiency\n", id, block, offset );
            warn_node_order++;
        }
        return 1;
      }
    }
        
#ifdef FIXED_POINT
    blocks[block].nodes[offset].lat = DOUBLE_TO_FIX(lat);
    blocks[block].nodes[offset].lon = DOUBLE_TO_FIX(lon);
#else
    blocks[block].nodes[offset].lat = lat;
    blocks[block].nodes[offset].lon = lon;
#endif
    blocks[block].used++;
    storedNodes++;
    return 0;
}


int pgsql_ram_nodes_get(struct osmNode *out, int id)
{
    int block  = id2block(id);
    int offset = id2offset(id);
    nodesCacheLookups++;

    if (!blocks[block].nodes)
        return 1;

    if (!blocks[block].nodes[offset].lat && !blocks[block].nodes[offset].lon)
        return 1;

#ifdef FIXED_POINT
    out->lat = FIX_TO_DOUBLE(blocks[block].nodes[offset].lat);
    out->lon = FIX_TO_DOUBLE(blocks[block].nodes[offset].lon);
#else
    out->lat = blocks[block].nodes[offset].lat;
    out->lon = blocks[block].nodes[offset].lon;
#endif
    nodesCacheHits++;
    return 0;
}

static void pgsql_cleanup(void)
{
    int i;

    for (i=0; i<num_tables; i++) {
        if (tables[i].sql_conn) {
            PQfinish(tables[i].sql_conn);
            tables[i].sql_conn = NULL;
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
static inline char *escape_tag( char *ptr, const char *in, int escape )
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

/* escape means we return '\N' for copy mode, otherwise we return just NULL */
char *pgsql_store_tags(struct keyval *tags, int escape)
{
  static char *buffer;
  static int buflen;

  char *ptr;
  struct keyval *i;
  int first;
    
  int countlist = countList(tags);
  if( countlist == 0 )
  {
    if( escape )
      return "\\N";
    else
      return NULL;
  }
    
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
    ptr = escape_tag( ptr, i->key, escape );
    *ptr++ = '"';
    *ptr++ = ',';
    *ptr++ = '"';
    ptr = escape_tag( ptr, i->value, escape );
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
  
  if( *string == '\0' )
    return;
    
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

static int pgsql_endCopy( struct table_desc *table)
{
    /* Terminate any pending COPY */
     if (table->copyMode) {
        PGconn *sql_conn = table->sql_conn;
        int stop = PQputCopyEnd(sql_conn, NULL);
        if (stop != 1) {
            fprintf(stderr, "COPY_END for %s failed: %s\n", table->copy, PQerrorMessage(sql_conn));
            exit_nicely();
        }

        PGresult *res = PQgetResult(sql_conn);
        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            fprintf(stderr, "COPY_END for %s failed: %s\n", table->copy, PQerrorMessage(sql_conn));
            PQclear(res);
            exit_nicely();
        }
        PQclear(res);
        table->copyMode = 0;
    }
    return 0;
}

static int pgsql_nodes_set(int id, double lat, double lon, struct keyval *tags)
{
    /* Four params: id, lat, lon, tags */
    char *paramValues[4];
    char *buffer;

    pgsql_ram_nodes_set( id, lat, lon, tags );
    if( node_table->copyMode )
    {
      char *tag_buf = pgsql_store_tags(tags,1);
      int length = strlen(tag_buf) + 64;
      buffer = alloca( length );
#ifdef FIXED_POINT
      if( snprintf( buffer, length, "%d\t%d\t%d\t%s\n", id, DOUBLE_TO_FIX(lat), DOUBLE_TO_FIX(lon), tag_buf ) > (length-10) )
      { fprintf( stderr, "buffer overflow node id %d\n", id); return 1; }
#else
      if( snprintf( buffer, length, "%d\t%.10f\t%.10f\t%s\n", id, lat, lon, tag_buf ) > (length-10) )
      { fprintf( stderr, "buffer overflow node id %d\n", id); return 1; }
#endif
      return pgsql_CopyData(__FUNCTION__, node_table->sql_conn, buffer);
    }
    buffer = alloca(64);
    paramValues[0] = buffer;
    paramValues[1] = paramValues[0] + sprintf( paramValues[0], "%d", id ) + 1;
#ifdef FIXED_POINT
    paramValues[2] = paramValues[1] + sprintf( paramValues[1], "%d", DOUBLE_TO_FIX(lat) ) + 1;
    sprintf( paramValues[2], "%d", DOUBLE_TO_FIX(lon) );
#else
    paramValues[2] = paramValues[1] + sprintf( paramValues[1], "%.10f", lat ) + 1;
    sprintf( paramValues[2], "%.10f", lon );
#endif
    paramValues[3] = pgsql_store_tags(tags,0);
    pgsql_execPrepared(node_table->sql_conn, "insert_node", 4, (const char * const *)paramValues, PGRES_COMMAND_OK);
    return 0;
}


static int pgsql_nodes_get(struct osmNode *out, int id)
{
    /* Check cache first */
    if( pgsql_ram_nodes_get( out, id ) == 0 )
      return 0;
      
    PGresult   *res;
    char tmp[16];
    char const *paramValues[1];
    PGconn *sql_conn = node_table->sql_conn;

    /* Make sure we're out of copy mode */
    pgsql_endCopy( node_table );

    snprintf(tmp, sizeof(tmp), "%d", id);
    paramValues[0] = tmp;
 
    res = pgsql_execPrepared(sql_conn, "get_node", 1, paramValues, PGRES_TUPLES_OK);

    if (PQntuples(res) != 1) {
        PQclear(res);
        return 1;
    } 

#ifdef FIXED_POINT
    out->lat = FIX_TO_DOUBLE(strtol(PQgetvalue(res, 0, 0), NULL, 10));
    out->lon = FIX_TO_DOUBLE(strtol(PQgetvalue(res, 0, 1), NULL, 10));
#else
    out->lat = strtod(PQgetvalue(res, 0, 0), NULL);
    out->lon = strtod(PQgetvalue(res, 0, 1), NULL);
#endif
    PQclear(res);
    return 0;
}

/* This should be made more efficient by using an IN(ARRAY[]) construct */
static int pgsql_nodes_get_list(struct osmNode *nodes, int *ndids, int nd_count)
{
    char tmp[16];
    char *tmp2; 
    int count, count2, countDB, countPG, i,j;
    int id;
    int *ndidspg;
    struct osmNode *nodespg;
    struct osmNode *nodes2;
    char const *paramValues[1]; 

    PGresult   *res;
    PGconn *sql_conn = node_table->sql_conn;
    
    count = 0; countDB = 0;

    tmp2 = malloc(sizeof(char)*nd_count*16);
    if (tmp2 == NULL) return 0; /*failed to allocate memory, return */

    /* create a list of ids in tmp2 to query the database  */
    snprintf(tmp2, sizeof(tmp2), "{");
    for( i=0; i<nd_count; i++ ) {
        /* Check cache first */ 
        if( pgsql_ram_nodes_get( &nodes[i], ndids[i]) == 0 ) {
            count++;
            continue;
        }
        countDB++;
        /* Mark nodes as needing to be fetched from the DB */
        nodes[i].lat = NAN;
        nodes[i].lon = NAN;
        snprintf(tmp, sizeof(tmp), "%d,", ndids[i]);
        strcat(tmp2,tmp);
    }
    tmp2[strlen(tmp2) - 1] = '}'; /* replace last , with } to complete list of ids*/
 
    if (countDB == 0) {
        free(tmp2);
        return count; /* All ids where in cache, so nothing more to do */
    }
 
    pgsql_endCopy( node_table ); 

    paramValues[0] = tmp2;  
    res = pgsql_execPrepared(sql_conn, "get_node_list", 1, paramValues, PGRES_TUPLES_OK);
    countPG = PQntuples(res);

    ndidspg = malloc(sizeof(int)*countPG);
    nodespg = malloc(sizeof(struct osmNode)*countPG);

    if ((ndidspg == NULL) || (nodespg == NULL)) {
        free(tmp2);
        free(ndidspg);
        free(nodespg);
        PQclear(res);
        return 0;
    }

    for (i = 0; i < countPG; i++) {
        ndidspg[i] = strtol(PQgetvalue(res, i, 0), NULL, 10); 
#ifdef FIXED_POINT 
        nodespg[i].lat = FIX_TO_DOUBLE(strtol(PQgetvalue(res, i, 1), NULL, 10)); 
        nodespg[i].lon = FIX_TO_DOUBLE(strtol(PQgetvalue(res, i, 2), NULL, 10)); 
#else 
        nodespg[i].lat = strtod(PQgetvalue(res, i, 1), NULL); 
        nodespg[i].lon = strtod(PQgetvalue(res, i, 2), NULL); 
#endif 
    }
 
 
    /* The list of results coming back from the db is in a different order to the list of nodes in the way.
       Match the results back to the way node list */
   
    for (i=0; i<nd_count; i++ )	{
        if ((isnan(nodes[i].lat)) || (isnan(nodes[i].lon))) {
            /* TODO: implement an O(log(n)) algorithm to match node ids */
            for (j = 0; j < countPG; j++) {
                if (ndidspg[j] == ndids[i]) {
                    nodes[i].lat = nodespg[j].lat;
                    nodes[i].lon = nodespg[j].lon;
                    count++;
                    break;
                }
            }
        }
    }

    /* If some of the nodes in the way don't exist, the returning list has wholes.
       As the rest of the code expects a continous list, it needs to be re-compacted */
    if (count != nd_count) {
        j = 0;
        for (i = 0; i < nd_count; i++) {
            if ( !isnan(nodes[i].lat)) {
                nodes[j].lat = nodes[i].lat;
                nodes[j].lon = nodes[i].lon;
                j++;
            }
         }
    }

    PQclear(res);
    free(tmp2);
    free(ndidspg);
    free(nodespg);

    return count;
}

static int pgsql_nodes_delete(int osm_id)
{
    char const *paramValues[1];
    char buffer[64];
    /* Make sure we're out of copy mode */
    pgsql_endCopy( node_table );
    
    sprintf( buffer, "%d", osm_id );
    paramValues[0] = buffer;
    pgsql_execPrepared(node_table->sql_conn, "delete_node", 1, paramValues, PGRES_COMMAND_OK );
    return 0;
}

static int pgsql_node_changed(int osm_id)
{
    char const *paramValues[1];
    char buffer[64];
    /* Make sure we're out of copy mode */
    pgsql_endCopy( way_table );
    pgsql_endCopy( rel_table );
    
    sprintf( buffer, "%d", osm_id );
    paramValues[0] = buffer;
    pgsql_execPrepared(way_table->sql_conn, "node_changed_mark", 1, paramValues, PGRES_COMMAND_OK );
    pgsql_execPrepared(rel_table->sql_conn, "node_changed_mark", 1, paramValues, PGRES_COMMAND_OK );
    return 0;
}

static int pgsql_ways_set(int way_id, int *nds, int nd_count, struct keyval *tags, int pending)
{
    /* Three params: id, nodes, tags, pending */
    char *paramValues[4];
    char *buffer;

    if( way_table->copyMode )
    {
      char *tag_buf = pgsql_store_tags(tags,1);
      char *node_buf = pgsql_store_nodes(nds, nd_count);
      int length = strlen(tag_buf) + strlen(node_buf) + 64;
      buffer = alloca(length);
      if( snprintf( buffer, length, "%d\t%s\t%s\t%c\n", 
              way_id, node_buf, tag_buf, pending?'t':'f' ) > (length-10) )
      { fprintf( stderr, "buffer overflow way id %d\n", way_id); return 1; }
      return pgsql_CopyData(__FUNCTION__, way_table->sql_conn, buffer);
    }
    buffer = alloca(64);
    paramValues[0] = buffer;
    paramValues[3] = paramValues[0] + sprintf( paramValues[0], "%d", way_id ) + 1;
    sprintf( paramValues[3], "%c", pending?'t':'f' );
    paramValues[1] = pgsql_store_nodes(nds, nd_count);
    paramValues[2] = pgsql_store_tags(tags,0);
    pgsql_execPrepared(way_table->sql_conn, "insert_way", 4, (const char * const *)paramValues, PGRES_COMMAND_OK);
    return 0;
}

/* Caller is responsible for freeing nodesptr & resetList(tags) */
static int pgsql_ways_get(int id, struct keyval *tags, struct osmNode **nodes_ptr, int *count_ptr)
{
    PGresult   *res;
    char tmp[16];
    char const *paramValues[1];
    PGconn *sql_conn = way_table->sql_conn;

    /* Make sure we're out of copy mode */
    pgsql_endCopy( way_table );

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
    PGconn *sql_conn = way_table->sql_conn;

    /* Make sure we're out of copy mode */
    pgsql_endCopy( way_table );

    snprintf(tmp, sizeof(tmp), "%d", id);
    paramValues[0] = tmp;
 
    pgsql_execPrepared(sql_conn, "way_done", 1, paramValues, PGRES_COMMAND_OK);

    return 0;
}

static int pgsql_ways_delete(int osm_id)
{
    char const *paramValues[1];
    char buffer[64];
    /* Make sure we're out of copy mode */
    pgsql_endCopy( way_table );
    
    sprintf( buffer, "%d", osm_id );
    paramValues[0] = buffer;
    pgsql_execPrepared(way_table->sql_conn, "delete_way", 1, paramValues, PGRES_COMMAND_OK );
    return 0;
}

static void pgsql_iterate_ways(int (*callback)(int id, struct keyval *tags, struct osmNode *nodes, int count, int exists))
{
    PGresult   *res_ways;
    int i, count = 0;
    /* The flag we pass to indicate that the way in question might exist already in the database */
    int exists = Append;

    fprintf(stderr, "\nGoing over pending ways\n");

    /* Make sure we're out of copy mode */
    pgsql_endCopy( way_table );
    
    res_ways = pgsql_execPrepared(way_table->sql_conn, "pending_ways", 0, NULL, PGRES_TUPLES_OK);

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
          
        callback(id, &tags, nodes, nd_count, exists);
        pgsql_ways_done( id );

        free(nodes);
        resetList(&tags);
    }

    PQclear(res_ways);
    fprintf(stderr, "\n");
}

static int pgsql_way_changed(int osm_id)
{
    char const *paramValues[1];
    char buffer[64];
    /* Make sure we're out of copy mode */
    pgsql_endCopy( rel_table );
    
    sprintf( buffer, "%d", osm_id );
    paramValues[0] = buffer;
    pgsql_execPrepared(rel_table->sql_conn, "way_changed_mark", 1, paramValues, PGRES_COMMAND_OK );
    return 0;
}

static int pgsql_rels_set(int id, struct member *members, int member_count, struct keyval *tags)
{
    /* Params: id, way_off, rel_off, parts, members, tags */
    char *paramValues[6];
    char *buffer;
    int i;
    struct keyval member_list;
    
    int node_parts[member_count], node_count = 0,
        ways_parts[member_count], ways_count = 0,
        rels_parts[member_count], rels_count = 0;
    
    int all_parts[member_count], all_count = 0;
    initList( &member_list );    
    for( i=0; i<member_count; i++ )
    {
      char tag = 0;
      switch( members[i].type )
      {
        case OSMTYPE_NODE:     node_parts[node_count++] = members[i].id; tag = 'n'; break;
        case OSMTYPE_WAY:      ways_parts[ways_count++] = members[i].id; tag = 'w'; break;
        case OSMTYPE_RELATION: rels_parts[rels_count++] = members[i].id; tag = 'r'; break;
        default: fprintf( stderr, "Internal error: Unknown member type %d\n", members[i].type ); exit_nicely();
      }
      char buf[64];
      sprintf( buf, "%c%d", tag, members[i].id );
      addItem( &member_list, buf, members[i].role, 0 );
    }
    memcpy( all_parts+all_count, node_parts, node_count*sizeof(int) ); all_count+=node_count;
    memcpy( all_parts+all_count, ways_parts, ways_count*sizeof(int) ); all_count+=ways_count;
    memcpy( all_parts+all_count, rels_parts, rels_count*sizeof(int) ); all_count+=rels_count;
  
    if( rel_table->copyMode )
    {
      char *tag_buf = strdup(pgsql_store_tags(tags,1));
      char *member_buf = pgsql_store_tags(&member_list,1);
      char *parts_buf = pgsql_store_nodes(all_parts, all_count);
      int length = strlen(member_buf) + strlen(tag_buf) + strlen(parts_buf) + 64;
      buffer = alloca(length);
      if( snprintf( buffer, length, "%d\t%d\t%d\t%s\t%s\t%s\tf\n", 
              id, node_count, node_count+ways_count, parts_buf, member_buf, tag_buf ) > (length-10) )
      { fprintf( stderr, "buffer overflow relation id %d\n", id); return 1; }
      free(tag_buf);
      resetList(&member_list);
      return pgsql_CopyData(__FUNCTION__, rel_table->sql_conn, buffer);
    }
    buffer = alloca(64);
    paramValues[0] = buffer;
    paramValues[1] = paramValues[0] + sprintf( paramValues[0], "%d", id ) + 1;
    paramValues[2] = paramValues[1] + sprintf( paramValues[1], "%d", node_count ) + 1;
    sprintf( paramValues[2], "%d", node_count+ways_count );
    paramValues[3] = pgsql_store_nodes(all_parts, all_count);
    paramValues[4] = pgsql_store_tags(&member_list,0);
    if( paramValues[4] )
        paramValues[4] = strdup(paramValues[4]);
    paramValues[5] = pgsql_store_tags(tags,0);
    pgsql_execPrepared(rel_table->sql_conn, "insert_rel", 6, (const char * const *)paramValues, PGRES_COMMAND_OK);
    if( paramValues[4] )
        free(paramValues[4]);
    resetList(&member_list);
    return 0;
}

/* Caller is responsible for freeing members & resetList(tags) */
static int pgsql_rels_get(int id, struct member **members, int *member_count, struct keyval *tags)
{
    PGresult   *res;
    char tmp[16];
    char const *paramValues[1];
    PGconn *sql_conn = rel_table->sql_conn;
    struct keyval member_temp;

    /* Make sure we're out of copy mode */
    pgsql_endCopy( rel_table );

    snprintf(tmp, sizeof(tmp), "%d", id);
    paramValues[0] = tmp;
 
    res = pgsql_execPrepared(sql_conn, "get_rel", 1, paramValues, PGRES_TUPLES_OK);
    /* Fields are: members, tags, member_count */

    if (PQntuples(res) != 1) {
        PQclear(res);
        return 1;
    } 

    pgsql_parse_tags( PQgetvalue(res, 0, 1), tags );
    initList(&member_temp);
    pgsql_parse_tags( PQgetvalue(res, 0, 0), &member_temp );

    int num_members = strtol(PQgetvalue(res, 0, 2), NULL, 10);
    struct member *list = malloc( sizeof(struct member)*num_members );
    
    int i=0;
    struct keyval *item;
    while( (item = popItem(&member_temp)) )
    {
        if( i >= num_members )
        {
            fprintf( stderr, "Unexpected member_count reading relation %d\n", id );
            exit_nicely();
        }
        char tag = item->key[0];
        list[i].type = (tag == 'n')?OSMTYPE_NODE:(tag == 'w')?OSMTYPE_WAY:(tag == 'r')?OSMTYPE_RELATION:-1;
        list[i].id = strtol(item->key+1, NULL, 10 );
        list[i].role = strdup( item->value );
        freeItem(item);
        i++;
    }
    *members = list;
    *member_count = num_members;
    PQclear(res);
    return 0;
}

static int pgsql_rels_done(int id)
{
    char tmp[16];
    char const *paramValues[1];
    PGconn *sql_conn = rel_table->sql_conn;

    /* Make sure we're out of copy mode */
    pgsql_endCopy( rel_table );

    snprintf(tmp, sizeof(tmp), "%d", id);
    paramValues[0] = tmp;
 
    pgsql_execPrepared(sql_conn, "rel_done", 1, paramValues, PGRES_COMMAND_OK);

    return 0;
}

static int pgsql_rels_delete(int osm_id)
{
    char const *paramValues[1];
    char buffer[64];
    /* Make sure we're out of copy mode */
    pgsql_endCopy( rel_table );
    
    sprintf( buffer, "%d", osm_id );
    paramValues[0] = buffer;
    pgsql_execPrepared(rel_table->sql_conn, "delete_rel", 1, paramValues, PGRES_COMMAND_OK );
    return 0;
}

static void pgsql_iterate_relations(int (*callback)(int id, struct member *members, int member_count, struct keyval *tags, int exists))
{
    PGresult   *res_rels;
    int i, count = 0;
    /* The flag we pass to indicate that the way in question might exist already in the database */
    int exists = Append;

    fprintf(stderr, "\nGoing over pending relations\n");

    /* Make sure we're out of copy mode */
    pgsql_endCopy( rel_table );
    
    res_rels = pgsql_execPrepared(rel_table->sql_conn, "pending_rels", 0, NULL, PGRES_TUPLES_OK);

    //fprintf(stderr, "\nIterating ways\n");
    for (i = 0; i < PQntuples(res_rels); i++) {
        int id = strtol(PQgetvalue(res_rels, i, 0), NULL, 10);
        struct keyval tags;
        struct member *members;
        int member_count;

        if (count++ %10 == 0)
                fprintf(stderr, "\rprocessing relation (%d)", count);

        initList(&tags);
        if( pgsql_rels_get(id, &members, &member_count, &tags) )
          continue;
          
        callback(id, members, member_count, &tags, exists);
        pgsql_rels_done( id );

        free(members);
        resetList(&tags);
    }

    PQclear(res_rels);
    fprintf(stderr, "\n");
}

static int pgsql_rel_changed(int osm_id)
{
    char const *paramValues[1];
    char buffer[64];
    /* Make sure we're out of copy mode */
    pgsql_endCopy( rel_table );
    
    sprintf( buffer, "%d", osm_id );
    paramValues[0] = buffer;
    pgsql_execPrepared(rel_table->sql_conn, "rel_changed_mark", 1, paramValues, PGRES_COMMAND_OK );
    return 0;
}

static void pgsql_analyze(void)
{
    int i;

    for (i=0; i<num_tables; i++) {
        PGconn *sql_conn = tables[i].sql_conn;
 
        if (tables[i].analyze) {
            pgsql_exec(sql_conn, PGRES_COMMAND_OK, "%s", tables[i].analyze);
        }
    }
}

static void pgsql_end(void)
{
    int i;

    for (i=0; i<num_tables; i++) {
        PGconn *sql_conn = tables[i].sql_conn;
 
        // Commit transaction
        if (tables[i].stop) {
            pgsql_exec(sql_conn, PGRES_COMMAND_OK, "%s", tables[i].stop);
        }

    }
}

/**
 * Helper to create SQL queries.
 * 
 * Takes four arguments:
 * prefix, t (tablespace) and i (index tablespace) - these may be NULL
 * a pointer to an input string which, on success, is changed to point to the 
 * result (caller takes ownership).
 *
 * The input string is mangled as follows:
 * %p replaced by the given prefix,
 * %i replaced by the given index tablespace
 * %t replaced by the given tablespace
 * other occurrences of the "%" char are treated normally.
 * any occurrence of { or } will be ignored (not copied to output string);
 * anything inside {} is only copied if it contained at least one of
 * %p, %i, %t that was not NULL.
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
static inline void set_prefix_and_tbls(const char *prefix, const char *t, const char *i, const char **string)
{
    char buffer[1024];
    if (*string == NULL) return;
    const char *source = *string;
    char *dest = buffer;
    char *openbrace = NULL;
    int copied = 0;

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
                if (prefix) {
                    strcpy(dest, prefix);
                    dest += strlen(prefix);
                    copied = 1;
                }
                source+=2;
                continue;
            } else if (*(source+1) == 't') {
                if (t) {
                    strcpy(dest, t);
                    dest += strlen(t);
                    copied = 1;
                }
                source+=2;
                continue;
            } else if (*(source+1) == 'i') {
                if (i) {
                    strcpy(dest, i);
                    dest += strlen(i);
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

static int build_indexes;

static int pgsql_start(const struct output_options *options)
{
    PGresult   *res;
    int i;
    int have_intarray = 0;
    int dropcreate = !options->append;

    scale = options->scale;
    Append = options->append;
    
    /* How much we can fit, and make sure it's odd */
    maxBlocks = (options->cache*((1024*1024)/(PER_BLOCK*sizeof(struct ramNode)))) | 1;
    queue = malloc( maxBlocks * sizeof(struct ramNodeBlock) );    
    
#ifdef __MINGW_H
    fprintf( stderr, "Mid: pgsql, scale=%d, cache=%dMB, maxblocks=%d*%d\n", scale, options->cache, maxBlocks, PER_BLOCK*sizeof(struct ramNode) ); 
#else
    fprintf( stderr, "Mid: pgsql, scale=%d, cache=%dMB, maxblocks=%d*%zd\n", scale, options->cache, maxBlocks, PER_BLOCK*sizeof(struct ramNode) );
#endif
    
    /* We use a connection per table to enable the use of COPY */
    for (i=0; i<num_tables; i++) {
        PGconn *sql_conn;
                        
        set_prefix_and_tbls(options->prefix, options->tblsslim_data, options->tblsslim_index, &(tables[i].name));
        set_prefix_and_tbls(options->prefix, options->tblsslim_data, options->tblsslim_index, &(tables[i].start));
        set_prefix_and_tbls(options->prefix, options->tblsslim_data, options->tblsslim_index, &(tables[i].create));
        set_prefix_and_tbls(options->prefix, options->tblsslim_data, options->tblsslim_index, &(tables[i].create_index));
        set_prefix_and_tbls(options->prefix, options->tblsslim_data, options->tblsslim_index, &(tables[i].prepare));
        set_prefix_and_tbls(options->prefix, options->tblsslim_data, options->tblsslim_index, &(tables[i].prepare_intarray));
        set_prefix_and_tbls(options->prefix, options->tblsslim_data, options->tblsslim_index, &(tables[i].copy));
        set_prefix_and_tbls(options->prefix, options->tblsslim_data, options->tblsslim_index, &(tables[i].analyze));
        set_prefix_and_tbls(options->prefix, options->tblsslim_data, options->tblsslim_index, &(tables[i].stop));
        set_prefix_and_tbls(options->prefix, options->tblsslim_data, options->tblsslim_index, &(tables[i].array_indexes));

        fprintf(stderr, "Setting up table: %s\n", tables[i].name);
        sql_conn = PQconnectdb(options->conninfo);

        /* Check to see that the backend connection was successfully made */
        if (PQstatus(sql_conn) != CONNECTION_OK) {
            fprintf(stderr, "Connection to database failed: %s\n", PQerrorMessage(sql_conn));
            exit_nicely();
        }
        tables[i].sql_conn = sql_conn;

        /* Not really the right place for this test, but we need a live
         * connection that not used for anything else yet, and we'd like to
         * warn users *before* we start doing mountains of work */
        if (i == t_node)
        {
            /* Note: this only checks for the GIST version, but recently there is also a GIN version, which may be faster... */
            res = PQexec(sql_conn, "select 1 from pg_opclass where opcname='gist__intbig_ops'" );
            if( PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) == 1 )
                have_intarray = 1;
            else
                fprintf( stderr, "*** WARNING: intarray contrib module not installed\n*** The resulting database will not be usable for applying diffs.\n" );
            PQclear(res);
            
            if( have_intarray && !options->append )
                build_indexes = 1;
        }
        if (dropcreate) {
            pgsql_exec(sql_conn, PGRES_COMMAND_OK, "DROP TABLE IF EXISTS %s", tables[i].name);
        }

        if (tables[i].start) {
            pgsql_exec(sql_conn, PGRES_COMMAND_OK, "%s", tables[i].start);
        }

        if (dropcreate && tables[i].create) {
            pgsql_exec(sql_conn, PGRES_COMMAND_OK, "%s", tables[i].create);
            if (tables[i].create_index) {
              pgsql_exec(sql_conn, PGRES_COMMAND_OK, "%s", tables[i].create_index);
            }
        }


        if (tables[i].prepare) {
            pgsql_exec(sql_conn, PGRES_COMMAND_OK, "%s", tables[i].prepare);
        }

        if (have_intarray && tables[i].prepare_intarray) {
            pgsql_exec(sql_conn, PGRES_COMMAND_OK, "%s", tables[i].prepare_intarray);
        }

        if (tables[i].copy) {
            pgsql_exec(sql_conn, PGRES_COPY_IN, "%s", tables[i].copy);
            tables[i].copyMode = 1;
        }
    }

    return 0;
}

static void *pgsql_stop_one(void *arg)
{
    struct table_desc *table = arg;
    PGconn *sql_conn = table->sql_conn;

    fprintf(stderr, "Stopping table: %s\n", table->name);
    pgsql_endCopy(table);
    if (table->stop) 
        pgsql_exec(sql_conn, PGRES_COMMAND_OK, "%s", table->stop);

    if (build_indexes && table->array_indexes) {
        char *buffer = (char *) malloc(strlen(table->array_indexes) + 99);
        char *tblsp = strstr(table->array_indexes, "TABLESPACE");

        // automatically insert FASTUPDATE=OFF when creating,
        // indexes for PostgreSQL 8.4 and higher
        // see http://lists.openstreetmap.org/pipermail/dev/2011-January/021704.html
        if (tblsp && PQserverVersion(sql_conn) >= 80400) {
            fprintf(stderr, "Building index on table: %s (fastupdate=off)\n", table->name);
            *tblsp = 0; // temporary null byte for following strcpy operation
            strcpy(buffer, table->array_indexes);
            *tblsp = 'T'; // restore old content
            strcat(buffer, " WITH (FASTUPDATE=OFF)");
            strcat(buffer, tblsp);
        } else {
            fprintf(stderr, "Building index on table: %s\n", table->name);
            strcpy(buffer, table->array_indexes);
        }
        pgsql_exec(sql_conn, PGRES_COMMAND_OK, "%s", buffer);
        free(buffer);
    }
    PQfinish(sql_conn);
    table->sql_conn = NULL;
    fprintf(stderr, "Stopped table: %s\n", table->name);
    return NULL;
}

static void pgsql_stop(void)
{
    int i;
#ifdef HAVE_PTHREAD
    pthread_t threads[num_tables];
#endif

    fprintf( stderr, "node cache: stored: %d(%.2f%%), storage efficiency: %.2f%%, hit rate: %.2f%%\n", 
             storedNodes, 100.0f*storedNodes/totalNodes, 100.0f*storedNodes/(usedBlocks*PER_BLOCK),
             100.0f*nodesCacheHits/nodesCacheLookups );
          
    for( i=0; i<usedBlocks; i++ )
    {
      free(queue[i]->nodes);
      queue[i]->nodes = NULL;
    }
    free(queue);

#ifdef HAVE_PTHREAD
    for (i=0; i<num_tables; i++) {
        int ret = pthread_create(&threads[i], NULL, pgsql_stop_one, &tables[i]);
        if (ret) {
            fprintf(stderr, "pthread_create() returned an error (%d)", ret);
            exit_nicely();
        }
    }
    for (i=0; i<num_tables; i++) {
        int ret = pthread_join(threads[i], NULL);
        if (ret) {
            fprintf(stderr, "pthread_join() returned an error (%d)", ret);
            exit_nicely();
        }
    }
#else
    for (i=0; i<num_tables; i++)
        pgsql_stop_one(&tables[i]);
#endif
}
 
struct middle_t mid_pgsql = {
        .start             = pgsql_start,
        .stop              = pgsql_stop,
        .cleanup           = pgsql_cleanup,
        .analyze           = pgsql_analyze,
        .end               = pgsql_end,

        .nodes_set         = pgsql_nodes_set,
#if 0
        .nodes_get         = pgsql_nodes_get,
#endif
        .nodes_get_list    = pgsql_nodes_get_list,
        .nodes_delete	   = pgsql_nodes_delete,
        .node_changed      = pgsql_node_changed,

        .ways_set          = pgsql_ways_set,
        .ways_get          = pgsql_ways_get,
        .ways_done         = pgsql_ways_done,
        .ways_delete       = pgsql_ways_delete,
        .way_changed       = pgsql_way_changed,

        .relations_set     = pgsql_rels_set,
#if 0
        .relations_get     = pgsql_rels_get,
#endif
        .relations_done    = pgsql_rels_done,
        .relations_delete  = pgsql_rels_delete,
        .relation_changed  = pgsql_rel_changed,
#if 0
        .iterate_nodes     = pgsql_iterate_nodes,
#endif
        .iterate_ways      = pgsql_iterate_ways,
        .iterate_relations = pgsql_iterate_relations
};
