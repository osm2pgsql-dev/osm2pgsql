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

#ifdef HAVE_PTHREAD
#include <pthread.h>
#endif

#include <libpq-fe.h>

#include "osmtypes.h"
#include "output.h"
#include "reprojection.h"
#include "output-pgsql.h"
#include "build_geometry.h"
#include "middle.h"
#include "pgsql.h"
#include "expire-tiles.h"
#include "wildcmp.h"
#include "node-ram-cache.h"
#include "tagtransform.h"

#define SRID (project_getprojinfo()->srs)

/* FIXME: Shouldn't malloc this all to begin with but call realloc()
   as required. The program will most likely segfault if it reads a
   style file with more styles than this */
#define MAX_STYLES 1000

enum table_id {
    t_point, t_line, t_poly, t_roads
};

static const struct output_options *Options;

/* enable output of a generated way_area tag to either hstore or its own column */
static int enable_way_area=1;

/* Tables to output */
static struct s_table {
    char *name;
    const char *type;
    PGconn *sql_conn;
    char buffer[1024];
    unsigned int buflen;
    int copyMode;
    char *columns;
} global_tables [] = {
    { .name = "%s_point",   .type = "POINT"     },
    { .name = "%s_line",    .type = "LINESTRING"},
    { .name = "%s_polygon", .type = "GEOMETRY"  }, /* Actually POLGYON & MULTIPOLYGON but no way to limit to just these two */
    { .name = "%s_roads",   .type = "LINESTRING"}
};
#define NUM_TABLES ((signed)(sizeof(global_tables) / sizeof(global_tables[0])))


static struct flagsname {
    char *name;
    int flag;
} tagflags[] = {
    { .name = "polygon",    .flag = FLAG_POLYGON },
    { .name = "linear",     .flag = FLAG_LINEAR },
    { .name = "nocache",    .flag = FLAG_NOCACHE },
    { .name = "delete",     .flag = FLAG_DELETE },
    { .name = "phstore",    .flag = FLAG_PHSTORE }
};
#define NUM_FLAGS ((signed)(sizeof(tagflags) / sizeof(tagflags[0])))



struct taginfo *exportList[4]; /* Indexed by enum table_id */
int exportListCount[4];


struct thread_ctx {
    void * geom_ctx;
    void * tagtransform_ctx;
    void * middle_ctx;
    struct s_table * tables;
};

static struct thread_ctx global_ctx;

#ifdef HAVE_PTHREAD
/**
 * Data structure to pass work from the main thread to the worker threads. This
 * allows to de-synchronize the threads with the main thread feeding work into the
 * pipe and the worker threads then taking out one element at a time and processing it.
 * It is a circular buffer storing up to 64 entries for each way and relation
 */
#define WORKER_THREAD_QUEUE_SIZE 64
static struct relation_info2 * rels_buffer[WORKER_THREAD_QUEUE_SIZE];
static struct way_info2 * ways_buffer[WORKER_THREAD_QUEUE_SIZE];
static rels_buffer_pfree = 0; //Pointer to the first free slot in the buffer
static rels_buffer_pfirst = 0;//Pointer to the first full slot in the buffer
static ways_buffer_pfree = 0; //Pointer to the first free slot in the buffer
static ways_buffer_pfirst = 0;//Pointer to the first full slot in the buffer


static pthread_t * worker_threads = NULL;
pthread_mutex_t lock_worker_queue = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond_worker_queue_work_available;
pthread_cond_t cond_worker_queue_space_available;
pthread_cond_t cond_worker_flush_middle; //Used to synchronize committing of middle layer in all threads on switching between ways and relations

static uint64_t way_inflight; //signals which threads have written ways to the middle layer and need to be flushed before switching to relations

//pthread_mutex_t lock_middle_processing = PTHREAD_MUTEX_INITIALIZER;

static volatile workers_finish = 0;
#endif

static int pgsql_delete_way_from_output(osmid_t osm_id, struct s_table * tables);
static int pgsql_delete_relation_from_output(osmid_t osm_id);
static int pgsql_process_relation(osmid_t id, struct member *members, int member_count, struct keyval *tags, int exists);
static int pgsql_out_connect2(const struct output_options *options, struct s_table * tables, int startTransaction);
static void pgsql_out_close2(int stopTransaction, struct s_table * tables);
static void pgsql_pause_copy(struct s_table *table);

void read_style_file( const char *filename )
{
  FILE *in;
  int lineno = 0;
  int num_read = 0;
  char osmtype[24];
  char tag[64];
  char datatype[24];
  char flags[128];
  int i;
  char *str;
  int fields;
  struct taginfo temp;
  char buffer[1024];
  int flag = 0;

  exportList[OSMTYPE_NODE] = malloc( sizeof(struct taginfo) * MAX_STYLES );
  exportList[OSMTYPE_WAY]  = malloc( sizeof(struct taginfo) * MAX_STYLES );

  in = fopen( filename, "rt" );
  if( !in )
  {
    fprintf( stderr, "Couldn't open style file '%s': %s\n", filename, strerror(errno) );
    exit_nicely();
  }
  
  while( fgets( buffer, sizeof(buffer), in) != NULL )
  {
    lineno++;
    
    str = strchr( buffer, '#' );
    if( str )
      *str = '\0';
      
    fields = sscanf( buffer, "%23s %63s %23s %127s", osmtype, tag, datatype, flags );
    if( fields <= 0 )  /* Blank line */
      continue;
    if( fields < 3 )
    {
      fprintf( stderr, "Error reading style file line %d (fields=%d)\n", lineno, fields );
      exit_nicely();
    }
    temp.name = strdup(tag);
    temp.type = strdup(datatype);
    
    temp.flags = 0;
    for( str = strtok( flags, ",\r\n" ); str; str = strtok(NULL, ",\r\n") )
    {
      for( i=0; i<NUM_FLAGS; i++ )
      {
        if( strcmp( tagflags[i].name, str ) == 0 )
        {
          temp.flags |= tagflags[i].flag;
          break;
        }
      }
      if( i == NUM_FLAGS )
        fprintf( stderr, "Unknown flag '%s' line %d, ignored\n", str, lineno );
    }
    if ((temp.flags!=FLAG_DELETE) && ((strchr(temp.name,'?') != NULL) || (strchr(temp.name,'*') != NULL))) {
        fprintf( stderr, "wildcard '%s' in non-delete style entry\n",temp.name);
        exit_nicely();
    }
    
    if ((0==strcmp(temp.name,"way_area")) && (temp.flags==FLAG_DELETE)) {
        enable_way_area=0;
    }

    temp.count = 0;
    /*    printf("%s %s %d %d\n", temp.name, temp.type, temp.polygon, offset ); */
    
    if( strstr( osmtype, "node" ) )
    {
      memcpy( &exportList[ OSMTYPE_NODE ][ exportListCount[ OSMTYPE_NODE ] ], &temp, sizeof(temp) );
      exportListCount[ OSMTYPE_NODE ]++;
      flag = 1;
    }
    if( strstr( osmtype, "way" ) )
    {
      memcpy( &exportList[ OSMTYPE_WAY ][ exportListCount[ OSMTYPE_WAY ] ], &temp, sizeof(temp) );
      exportListCount[ OSMTYPE_WAY ]++;
      flag = 1;
    }
    if( !flag )
    {
      fprintf( stderr, "Weird style line %d\n", lineno );
      exit_nicely();
    }
    num_read++;
  }
  if (ferror(in)) {
      perror(filename);
      exit_nicely();
  }
  if (num_read == 0) {
      fprintf(stderr, "Unable to parse any valid columns from the style file. Aborting.\n");
      exit_nicely();
  }
  fclose(in);
}

static void free_style_refs(const char *name, const char *type)
{
    /* Find and remove any other references to these pointers
       This would be way easier if we kept a single list of styles
       Currently this scales with n^2 number of styles */
    int i,j;

    for (i=0; i<NUM_TABLES; i++) {
        for(j=0; j<exportListCount[i]; j++) {
            if (exportList[i][j].name == name)
                exportList[i][j].name = NULL;
            if (exportList[i][j].type == type)
                exportList[i][j].type = NULL;
        }
    }
}

static void free_style(void)
{
    int i, j;
    for (i=0; i<NUM_TABLES; i++) {
        for(j=0; j<exportListCount[i]; j++) {
            free(exportList[i][j].name);
            free(exportList[i][j].type);
            free_style_refs(exportList[i][j].name, exportList[i][j].type);
        }
    }
    for (i=0; i<NUM_TABLES; i++)
        free(exportList[i]);
}

/* Handles copying out, but coalesces the data into large chunks for
 * efficiency. PostgreSQL doesn't actually need this, but each time you send
 * a block of data you get 5 bytes of overhead. Since we go column by column
 * with most empty and one byte delimiters, without this optimisation we
 * transfer three times the amount of data necessary.
 */
static void copy_to_table(struct s_table * table, const char *sql)
{
    PGconn *sql_conn = table->sql_conn;
    unsigned int len = strlen(sql);
    unsigned int buflen = table->buflen;
    char *buffer = table->buffer;

    /* Return to copy mode if we dropped out */
    if( !table->copyMode )
    {
        pgsql_exec(sql_conn, PGRES_COPY_IN, "COPY %s (%s,way) FROM STDIN", table->name, table->columns);
        table->copyMode = 1;
    }
    /* If the combination of old and new data is too big, flush old data */
    if( (unsigned)(buflen + len) > sizeof( table->buffer )-10 )
    {
      pgsql_CopyData(table->name, sql_conn, buffer);
      buflen = 0;

      /* If new data by itself is also too big, output it immediately */
      if( (unsigned)len > sizeof( table->buffer )-10 )
      {
        pgsql_CopyData(table->name, sql_conn, sql);
        len = 0;
      }
    }
    /* Normal case, just append to buffer */
    if( len > 0 )
    {
      strcpy( buffer+buflen, sql );
      buflen += len;
      len = 0;
    }

    /* If we have completed a line, output it */
    if( buflen > 0 && buffer[buflen-1] == '\n' )
    {
      pgsql_CopyData(table->name, sql_conn, buffer);
      buflen = 0;
    }

    table->buflen = buflen;
}




static void pgsql_out_cleanup(void * tables_p)
{
    struct s_table * tables = tables_p;
    int i;

    for (i=0; i<NUM_TABLES; i++) {
        if (tables[i].sql_conn) {
            PQfinish(tables[i].sql_conn);
            tables[i].sql_conn = NULL;
        }
    }
}

/* Escape data appropriate to the type */
static void escape_type(char *sql, int len, const char *value, const char *type) {
    int items;
    char *tmpstr;

    tmpstr = malloc(len);
    strcpy(tmpstr, value);

    if (!strcmp(type, "int4")) {
        int from, to;
        /* For integers we take the first number, or the average if it's a-b */
        items = sscanf(value, "%d-%d", &from, &to);
        if (items == 1) {
            sprintf(sql, "%d", from);
        } else if (items == 2) {
            sprintf(sql, "%d", (from + to) / 2);
        } else {
            sprintf(sql, "\\N");
        }
    } else {
        /*
         try to "repair" real values as follows:
         * assume "," to be a decimal mark which need to be replaced by "."
         * like int4 take the first number, or the average if it's a-b
         * assume SI unit (meters)
         * convert feet to meters (1 foot = 0.3048 meters)
         * reject anything else
         */
        if (!strcmp(type, "real")) {
            int i, slen;
            float from, to;

            slen = strlen(value);
            for (i = 0; i < slen; i++)
                if (tmpstr[i] == ',')
                    tmpstr[i] = '.';

            items = sscanf(tmpstr, "%f-%f", &from, &to);
            if (items == 1) {
                if ((tmpstr[slen - 2] == 'f') && (tmpstr[slen - 1] == 't')) {
                    from *= 0.3048;
                }
                sprintf(sql, "%f", from);
            } else if (items == 2) {
                if ((tmpstr[slen - 2] == 'f') && (tmpstr[slen - 1] == 't')) {
                    from *= 0.3048;
                    to *= 0.3048;
                }
                sprintf(sql, "%f", (from + to) / 2);
            } else {
                sprintf(sql, "\\N");
            }
        } else {
            escape(sql, len, value);
        }
    }
    free(tmpstr);
}

static void write_hstore(struct s_table * table, struct keyval *tags)
{
    char *sql;
    size_t sqllen=0;
    size_t hlen;
    /* a clone of the tags pointer */
    struct keyval *xtags = tags;
        
    /* sql buffer */
    sqllen=2048;
    sql=malloc(sqllen);

    
    /* while this tags has a follow-up.. */
    while (xtags->next->key != NULL)
    {

      /* hard exclude z_order tag and keys which have their own column */
      if ((xtags->next->has_column) || (strcmp("z_order",xtags->next->key)==0)) {
          /* update the tag-pointer to point to the next tag */
          xtags = xtags->next;
          continue;
      }

      /*
        hstore ASCII representation looks like
        "<key>"=>"<value>"
        
        we need at least strlen(key)+strlen(value)+6+'\0' bytes
        in theory any single character could also be escaped
        thus we need an additional factor of 2.
        The maximum lenght of a single hstore element is thus
        calcuated as follows:
      */
      hlen=2 * (strlen(xtags->next->key) + strlen(xtags->next->value)) + 7;
      
      /* if the sql buffer is too small */
      if (hlen > sqllen) {
        sqllen = hlen;
        sql = realloc(sql, sqllen);
      }
        
      /* pack the tag with its value into the hstore */
      keyval2hstore(sql, xtags->next);
      copy_to_table(table, sql);

      /* update the tag-pointer to point to the next tag */
      xtags = xtags->next;
        
      /* if the tag has a follow up, add a comma to the end */
      if (xtags->next->key != NULL)
          copy_to_table(table, ",");
    }
    
    /* finish the hstore column by placing a TAB into the data stream */
    copy_to_table(table, "\t");
    free(sql);
    /* the main hstore-column has now been written */
}

/* write an hstore column to the database */
static void write_hstore_columns(struct s_table * table, struct keyval *tags)
{
    char *sql;
    int sqllen=0;
    char *shortkey;
    /* the index of the current hstore column */
    int i_hstore_column;
    int found;
    struct keyval *xtags;
    char *pos;
    size_t hlen;
    
    /* sql buffer */
    sqllen=2048;
    sql=malloc(sqllen);
    
    /* iterate over all configured hstore colums in the options */
    for(i_hstore_column = 0; i_hstore_column < Options->n_hstore_columns; i_hstore_column++)
    {
        /* did this node have a tag that matched the current hstore column */
        found = 0;
        
        /* a clone of the tags pointer */
        xtags = tags;
        
        /* while this tags has a follow-up.. */
        while (xtags->next->key != NULL) {
            
            /* check if the tag's key starts with the name of the hstore column */
            pos = strstr(xtags->next->key, Options->hstore_columns[i_hstore_column]);
            
            /* and if it does.. */
            if(pos == xtags->next->key)
            {
                /* remember we found one */
                found=1;
                
                /* generate the short key name */
                shortkey = xtags->next->key + strlen(Options->hstore_columns[i_hstore_column]);
                
                /* calculate the size needed for this hstore entry */
                hlen=2*(strlen(shortkey)+strlen(xtags->next->value))+7;
                
                /* if the sql buffer is too small */
                if (hlen > sqllen) {
                    /* resize it */
                    sqllen=hlen;
                    sql=realloc(sql,sqllen);
                }
                
                /* and pack the shortkey with its value into the hstore */
                keyval2hstore_manual(sql, shortkey, xtags->next->value);
                copy_to_table(table, sql);
                
                /* update the tag-pointer to point to the next tag */
                xtags=xtags->next;
                
                /* if the tag has a follow up, add a comma to the end */
                if (xtags->next->key != NULL)
                    copy_to_table(table, ",");
            }
            else
            {
                /* update the tag-pointer to point to the next tag */
                xtags=xtags->next;
            }
        }
        
        /* if no matching tag has been found, write a NULL */
        if(!found)
            copy_to_table(table, "\\N");
        
        /* finish the hstore column by placing a TAB into the data stream */
        copy_to_table(table, "\t");
    }
    
    free(sql);

    /* all hstore-columns have now been written */
}


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

static int pgsql_out_node(osmid_t id, struct keyval *tags, double node_lat, double node_lon)
{

    int filter = tagtransform_filter_node_tags(global_ctx.tagtransform_ctx, tags);
    static char *sql;
    static size_t sqllen=0;
    int i;
    struct keyval *tag;

    if (filter) return 1;

    if (sqllen==0) {
      sqllen=2048;
      sql=malloc(sqllen);
    }

    expire_tiles_from_bbox(node_lon, node_lat, node_lon, node_lat);
    sprintf(sql, "%" PRIdOSMID "\t", id);
    copy_to_table(&(global_tables[t_point]), sql);

    for (i=0; i < exportListCount[OSMTYPE_NODE]; i++) {
        if( exportList[OSMTYPE_NODE][i].flags & FLAG_DELETE )
            continue;
        if( (exportList[OSMTYPE_NODE][i].flags & FLAG_PHSTORE) == FLAG_PHSTORE)
            continue;
        if ((tag = getTag(tags, exportList[OSMTYPE_NODE][i].name)))
        {
            escape_type(sql, sqllen, tag->value, exportList[OSMTYPE_NODE][i].type);
            exportList[OSMTYPE_NODE][i].count++;
            if (HSTORE_NORM==Options->enable_hstore)
                tag->has_column=1;
        }
        else
            sprintf(sql, "\\N");

        copy_to_table(&(global_tables[t_point]), sql);
        copy_to_table(&(global_tables[t_point]), "\t");
    }
    
    /* hstore columns */
    write_hstore_columns(&(global_tables[t_point]), tags);
    
    /* check if a regular hstore is requested */
    if (Options->enable_hstore)
        write_hstore(&(global_tables[t_point]), tags);
    
#ifdef FIXED_POINT
    // guarantee that we use the same values as in the node cache
    scale = Options->scale;
    node_lon = FIX_TO_DOUBLE(DOUBLE_TO_FIX(node_lon));
    node_lat = FIX_TO_DOUBLE(DOUBLE_TO_FIX(node_lat));
#endif

    sprintf(sql, "SRID=%d;POINT(%.15g %.15g)", SRID, node_lon, node_lat);
    copy_to_table(&(global_tables[t_point]), sql);
    copy_to_table(&(global_tables[t_point]), "\n");

    return 0;
}



static void write_wkts(osmid_t id, struct keyval *tags, const char *wkt, struct s_table * table)
{
  
    char *sql;
    size_t sqllen=0;
    int j;
    struct keyval *tag;

    sqllen=2048;
    sql=malloc(sqllen);
    
    sprintf(sql, "%" PRIdOSMID "\t", id);
    copy_to_table(table, sql);

    for (j=0; j < exportListCount[OSMTYPE_WAY]; j++) {
            if( exportList[OSMTYPE_WAY][j].flags & FLAG_DELETE )
                continue;
            if( (exportList[OSMTYPE_WAY][j].flags & FLAG_PHSTORE) == FLAG_PHSTORE)
                continue;
            if ((tag = getTag(tags, exportList[OSMTYPE_WAY][j].name)))
            {
                exportList[OSMTYPE_WAY][j].count++;
                escape_type(sql, sqllen, tag->value, exportList[OSMTYPE_WAY][j].type);
                if (HSTORE_NORM==Options->enable_hstore)
                    tag->has_column=1;
            }
            else
                sprintf(sql, "\\N");

            copy_to_table(table, sql);
            copy_to_table(table, "\t");
    }
    
    /* hstore columns */
    write_hstore_columns(table, tags);
    
    /* check if a regular hstore is requested */
    if (Options->enable_hstore)
        write_hstore(table, tags);
    
    sprintf(sql, "SRID=%d;", SRID);
    copy_to_table(table, sql);
    copy_to_table(table, wkt);
    copy_to_table(table, "\n");
    free(sql);
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
static int pgsql_out_way_single(struct thread_ctx * ctx, struct way_info * way) {
    int polygon = 0, roads = 0;
    int i, wkt_size;
    double split_at;
    double area;

    /* If the flag says this object may exist already, delete it first */
    if (way->exists) {
        pgsql_delete_way_from_output(way->id, ctx->tables);
        Options->mid->way_changed(ctx->middle_ctx, way->id);
    }

    if (tagtransform_filter_way_tags(ctx->tagtransform_ctx, way->tags, &polygon, &roads)) {
        resetList(way->tags);
        free(way->tags);
        free(way->nodes);
        free(way);
        return 0;
    }
    /* Split long ways after around 1 degree or 100km */
    if (Options->projection == PROJ_LATLONG)
        split_at = 1;
    else
        split_at = 100 * 1000;

    wkt_size = get_wkt_split(ctx->geom_ctx, way->nodes, way->node_count, polygon,
            split_at);

    for (i = 0; i < wkt_size; i++) {
        char *wkt = get_wkt(ctx->geom_ctx, i);

        if (wkt && strlen(wkt)) {
            /* FIXME: there should be a better way to detect polygons */
            if (!strncmp(wkt, "POLYGON", strlen("POLYGON"))
                    || !strncmp(wkt, "MULTIPOLYGON", strlen("MULTIPOLYGON"))) {
                expire_tiles_from_nodes_poly(way->nodes, way->node_count,
                        way->id);
                area = get_area(ctx->geom_ctx, i);
                if ((area > 0.0) && enable_way_area) {
                    char tmp[32];
                    snprintf(tmp, sizeof(tmp), "%g", area);
                    addItem(way->tags, "way_area", tmp, 0);
                }
                write_wkts(way->id, way->tags, wkt, &(ctx->tables[t_poly]));
            } else {
                expire_tiles_from_nodes_line(way->nodes, way->node_count);
                write_wkts(way->id, way->tags, wkt, &(ctx->tables[t_line]));
                if (roads) {
                    write_wkts(way->id, way->tags, wkt, &(ctx->tables[t_roads]));
                }
            }
        }
        free(wkt);
    }
    clear_wkts(ctx->geom_ctx);

    resetList(way->tags);
    free(way->tags);
    free(way->nodes);
    free(way);

    return 0;
}

static int pgsql_add_way_single(struct thread_ctx * ctx, struct way_info2 * way) {
  int polygon = 0;
  int roads = 0;
  struct way_info * way_full;


  /* Check whether the way is: (1) Exportable, (2) Maybe a polygon */
  int filter = tagtransform_filter_way_tags(ctx->tagtransform_ctx, way->tags, &polygon, &roads);

  /* If this isn't a polygon then it can not be part of a multipolygon
     Hence only polygons are "pending" */
  Options->mid->ways_set(ctx->middle_ctx, way->id, way->nds, way->nd_count, way->tags, (!filter && polygon) ? 1 : 0);

  if( !polygon && !filter )
  {
    /* Get actual node data and generate output */
    struct osmNode *nodes = malloc( sizeof(struct osmNode) * way->nd_count );
    int count = Options->mid->nodes_get_list(ctx->middle_ctx, nodes, way->nds, way->nd_count );
    way_full = (struct way_info *)malloc(sizeof(struct way_info));
    way_full->id = way->id;
    way_full->tags = way->tags;
    way_full->exists = 0;
    way_full->node_count = count;
    way_full->nodes = nodes;
    pgsql_out_way_single(ctx, way_full);

  } else {
      resetList(way->tags);
      free(way->tags);
  }
  free(way->nds);
  free(way);

  return 0;
}


static void free_rel_struct(struct relation_info * rel) {
    int i;
    for( i =0; i<rel->member_count; i++ ) {
        resetList( &(rel->member_tags[i]) );
        free( rel->member_way_nodes[i] );
        free( rel->member_roles[i]);
    }

    if (rel->member_ids) free(rel->member_ids);
    if (rel->member_tags) free(rel->member_tags);
    if (rel->member_way_node_count) free(rel->member_way_node_count);
    if (rel->member_way_nodes) free(rel->member_way_nodes);
    if (rel->member_roles) free(rel->member_roles);
    resetList(rel->tags);
    free(rel->tags);
    free(rel);
}



static int pgsql_out_relation_single(struct relation_info * rel, struct thread_ctx * ctx) {
    int * members_superseeded;
    int make_polygon = 0;
    int make_boundary = 0;
    int polygon = 0, roads = 0;
    char *type;
    double split_at;
    int i, wkt_size;

    members_superseeded = calloc(sizeof(int), rel->member_count);

    if (rel->member_count == 0) {
        free(members_superseeded);
        free_rel_struct(rel);
        return 0;
    }

    if (tagtransform_filter_rel_member_tags(ctx->tagtransform_ctx, rel->tags, rel->member_count,
            rel->member_tags, rel->member_roles, members_superseeded,
            &make_boundary, &make_polygon, &roads)) {
        free(members_superseeded);
        free_rel_struct(rel);
        return 0;
    }

    /* Split long linear ways after around 1 degree or 100km (polygons not effected) */
    if (Options->projection == PROJ_LATLONG)
        split_at = 1;
    else
        split_at = 100 * 1000;

    wkt_size = build_geometry(ctx->geom_ctx, rel->id, rel->member_way_nodes,
            rel->member_way_node_count, make_polygon, Options->enable_multi,
            split_at);

    if (!wkt_size) {
        free(members_superseeded);
        free_rel_struct(rel);
        return 0;
    }

    for (i = 0; i < wkt_size; i++) {
        char *wkt = get_wkt(ctx->geom_ctx, i);

        if (wkt && strlen(wkt)) {
            expire_tiles_from_wkt(wkt, -rel->id);
            /* FIXME: there should be a better way to detect polygons */
            if (!strncmp(wkt, "POLYGON", strlen("POLYGON"))
                    || !strncmp(wkt, "MULTIPOLYGON", strlen("MULTIPOLYGON"))) {
                double area = get_area(ctx->geom_ctx, i);
                if ((area > 0.0) && enable_way_area) {
                    char tmp[32];
                    snprintf(tmp, sizeof(tmp), "%g", area);
                    addItem(rel->tags, "way_area", tmp, 0);
                }
                write_wkts(-rel->id, rel->tags, wkt, &(ctx->tables[t_poly]));
            } else {
                write_wkts(-rel->id, rel->tags, wkt, &(ctx->tables[t_line]));
                if (roads)
                    write_wkts(-rel->id, rel->tags, wkt, &(ctx->tables[t_roads]));
            }
        }
        free(wkt);
    }

    clear_wkts(ctx->geom_ctx);

    /* Tagtransform will have marked those member ways of the relation that
     * have fully been dealt with as part of the multi-polygon entry.
     * Set them in the database as done and delete their entry to not
     * have duplicates */
    if (make_polygon) {
        for (i = 0; rel->member_way_node_count[i]; i++) {
            if (members_superseeded[i]) {
                //TODO: Need to find a thread-safe way to do the done marking
                Options->mid->ways_done(ctx->middle_ctx, rel->member_ids[i]);
                pgsql_delete_way_from_output(rel->member_ids[i], ctx->tables);
            }
        }
    }

    free(members_superseeded);

    /* If we are making a boundary then also try adding any relations which form complete rings
     The linear variants will have already been processed above */
    if (make_boundary) {
        wkt_size = build_geometry(ctx->geom_ctx, rel->id, rel->member_way_nodes,
                rel->member_way_node_count, 1, Options->enable_multi, split_at);
        for (i = 0; i < wkt_size; i++) {
            char *wkt = get_wkt(ctx->geom_ctx, i);

            if (strlen(wkt)) {
                expire_tiles_from_wkt(wkt, -rel->id);
                /* FIXME: there should be a better way to detect polygons */
                if (!strncmp(wkt, "POLYGON", strlen("POLYGON"))
                        || !strncmp(wkt, "MULTIPOLYGON",
                                strlen("MULTIPOLYGON"))) {
                    double area = get_area(ctx->geom_ctx, i);
                    if ((area > 0.0) && enable_way_area) {
                        char tmp[32];
                        snprintf(tmp, sizeof(tmp), "%g", area);
                        addItem(rel->tags, "way_area", tmp, 0);
                    }
                    write_wkts(-rel->id, rel->tags, wkt, &ctx->tables[t_poly]);
                }
            }
            free(wkt);
        }
        clear_wkts(ctx->geom_ctx);
    }

    free_rel_struct(rel);

    return 1;
}

/* This is the workhorse of pgsql_add_relation, split out because it is used as the callback for iterate relations */
static int pgsql_process_relation_single(struct thread_ctx * ctx, struct relation_info2 * rel) {
    struct relation_info * rel_full = calloc(1,sizeof(struct relation_info));
    int i, j, count;
    osmid_t *xid2 = malloc( (rel->member_count+1) * sizeof(osmid_t) );
    int filter;

  rel_full->id = rel->id;
  rel_full->tags = rel->tags;

  rel_full->member_roles = malloc( (rel->member_count+1) * sizeof(const char *) );
  rel_full->member_way_node_count = malloc( (rel->member_count+1) * sizeof(int) );
  rel_full->member_tags = malloc( (rel->member_count+1) * sizeof(struct keyval) );
  rel_full->member_way_nodes = malloc( (rel->member_count+1) * sizeof(struct osmNode*) );

  /* If the flag says this object may exist already, delete it first */
  if(rel->exists)
      pgsql_delete_relation_from_output(rel->id);

  if (tagtransform_filter_rel_tags(ctx->tagtransform_ctx, rel->tags)) {
      free_rel_struct(rel_full);
      return 1;
  }

  count = 0;
  for( i=0; i < rel->member_count; i++ ) {
    /* Need to handle more than just ways... */
    if( rel->members[i].type != OSMTYPE_WAY )
        continue;
    xid2[count] = rel->members[i].id;
    count++;
  }

  rel_full->member_count = Options->mid->ways_get_list(ctx->middle_ctx, xid2, count, &rel_full->member_ids, rel_full->member_tags, rel_full->member_way_nodes, rel_full->member_way_node_count);

  for (i = 0; i < rel_full->member_count; i++) {
      for (j = i; j < rel->member_count; j++) {
          if (rel->members[j].id == rel_full->member_ids[i]) break;
      }
      rel_full->member_roles[i] = strdup(rel->members[j].role);
  }
  rel_full->member_way_nodes[rel_full->member_count] = NULL;
  rel_full->member_way_node_count[rel_full->member_count] = 0;
  rel_full->member_ids[rel_full->member_count] = 0;
  rel_full->member_roles[rel_full->member_count] = NULL;

  for (i = 0; i < rel->member_count; i++) {
      free(rel->members[i].role);
  }
  free(rel->members);
  free(rel);

  /* At some point we might want to consider storing the retrieved data in the members, rather than as separate arrays */
  pgsql_out_relation_single(rel_full, ctx);

  free(xid2);
  return 0;
}

/**
 * This is the thread function for the worker threads. It takes an element from the work queue and passes it on to the
 * actual processing function.
 */
#ifdef HAVE_PTHREAD
static void * pgsql_worker_thread(void * pointer) {
    struct relation_info2 * rel;
    struct way_info2 * way;
    struct thread_ctx ctx;
    int * thread_id = pointer;


    /*
     * We need a new set of connections to postgresql in this thread
     */
    ctx.tables = malloc(sizeof(global_tables));
    memcpy(ctx.tables, global_tables, sizeof(global_tables));
    pgsql_out_connect2(Options, ctx.tables, 0);

    ctx.geom_ctx = init_geometry_ctx(); //create a new geometry ctx for this thread
    ctx.tagtransform_ctx = tagtransform_init(Options);
    if (Options->mid->connect) {
        ctx.middle_ctx = Options->mid->connect(Options);
    } else ctx.middle_ctx = global_ctx.middle_ctx;

    while ((workers_finish == 0) || (ways_buffer_pfirst != ways_buffer_pfree) || (rels_buffer_pfirst != rels_buffer_pfree)) {
        pthread_mutex_lock(&lock_worker_queue);
        while ((ways_buffer_pfirst == ways_buffer_pfree) && (rels_buffer_pfirst == rels_buffer_pfree)) {
            pthread_cond_wait(&cond_worker_queue_work_available, &lock_worker_queue);
            if (workers_finish) { //We are done and trying to exit.
                pthread_mutex_unlock(&lock_worker_queue);
                break;
            }
        }
        //If we exited the while loop without there actually being work, then presumably we are done and want to exit the thread.
        if ((ways_buffer_pfirst == ways_buffer_pfree) && (rels_buffer_pfirst == rels_buffer_pfree)) continue;

        way = NULL;
        rel = NULL;
        if (ways_buffer_pfirst != ways_buffer_pfree) { //We have an element in the way queue, process it.
            way = ways_buffer[ways_buffer_pfirst];
            ways_buffer_pfirst++;
            if (ways_buffer_pfirst > (WORKER_THREAD_QUEUE_SIZE - 1)) ways_buffer_pfirst = 0; // circular buffer wrap around
            way_inflight |= ((uint64_t)1 << *thread_id); //We will likely have written a way to the middle layer, so we need to remember to flush it before moving on.
        } else {
            rel = rels_buffer[rels_buffer_pfirst];
            rels_buffer_pfirst++;
            if (rels_buffer_pfirst > (WORKER_THREAD_QUEUE_SIZE - 1)) rels_buffer_pfirst = 0; // circular buffer wrap around
        }
        pthread_mutex_unlock(&lock_worker_queue);
        pthread_cond_signal(&cond_worker_queue_space_available);
        if (way) pgsql_add_way_single(&ctx, way);
        if (rel) {
            /* Before processing relations, we need to make sure that all ways in the middle layer have been
             * committed and visible to all threads, as otherwise we might not be able to retrieve all ways in
             * the relation processing.
             */
            if ((way_inflight & ((uint64_t)1 << *thread_id)) > 0) { //Check if this thread needs committing
                Options->mid->commit(ctx.middle_ctx);
                pthread_mutex_lock(&lock_worker_queue);
                way_inflight ^= ((uint64_t)1 << *thread_id);
                if (way_inflight == 0) {
                    pthread_cond_broadcast(&cond_worker_flush_middle);
                }
                pthread_mutex_unlock(&lock_worker_queue);
            }
            pthread_mutex_lock(&lock_worker_queue);
            while (way_inflight) {
                pthread_cond_wait(&cond_worker_flush_middle, &lock_worker_queue);
            }
            pthread_mutex_unlock(&lock_worker_queue);
            pgsql_process_relation_single(&ctx, rel);
        }

    }
    //We are done, closing worker thread.
    Options->mid->commit(ctx.middle_ctx);
    Options->mid->cleanup(ctx.middle_ctx);
    pgsql_out_close2(0, ctx.tables);
    free(ctx.tables);
    close_geometry_ctx(ctx.geom_ctx);
    tagtransform_shutdown(ctx.tagtransform_ctx);
    free(thread_id);
    return NULL;
}
#endif //HAVE_PTHREAD

static int pgsql_out_way(osmid_t id, struct keyval *tags, struct osmNode *nodes, int count, int exists) {
    int i;
    struct way_info * way;
    //Create a worker package to put in the queue.
    way = (struct way_info *)malloc(sizeof(struct way_info));
    way->id = id;
    /* We need to duplicate the tags structure here, as the calling function of pgsql_out_way
     * currently assumes it can free the tags. However, they can only be freed once the worker
     * threads have actually finished the processing after which they will free the tags structure
     * of the work package.
     */
    way->tags = malloc(sizeof(struct keyval));
    initList(way->tags);
    cloneList(way->tags, tags);
    way->nodes = nodes;
    way->node_count = count;
    way->exists = exists;

    /* The external callers of pgsql_out_way assume they can free the nodes
     * array. However due to the asynchronisity of the worker thread model,
     * the pgsql_out_way_single function frees the nodes array. Due to the
     * mismatch in the API, duplicate the nodes array, that both caller and
     * pgsql_out_way_single can free the array.
     * TODO: Find a better way, by fixing upstream of pgsql_out_way()
     */

    way->nodes = malloc(sizeof(struct osmNode) * count);
    memcpy(way->nodes, nodes, sizeof(struct osmNode) * count);
    return pgsql_out_way_single(&global_ctx, way);
}


static int pgsql_out_connect2(const struct output_options *options, struct s_table * tables, int startTransaction) {
    int i;
    for (i=0; i<NUM_TABLES; i++) {
        PGconn *sql_conn;
        sql_conn = PQconnectdb(options->conninfo);
        
        /* Check to see that the backend connection was successfully made */
        if (PQstatus(sql_conn) != CONNECTION_OK) {
            fprintf(stderr, "Connection to database failed: %s\n", PQerrorMessage(sql_conn));
            return 1;
        }
        tables[i].sql_conn = sql_conn;
        pgsql_exec(sql_conn, PGRES_COMMAND_OK, "SET synchronous_commit TO off;");
        pgsql_exec(sql_conn, PGRES_COMMAND_OK, "PREPARE get_wkt (" POSTGRES_OSMID_TYPE ") AS SELECT ST_AsText(way) FROM %s WHERE osm_id = $1;\n", tables[i].name);
        if (startTransaction)
            pgsql_exec(sql_conn, PGRES_COMMAND_OK, "BEGIN");
        tables[i].copyMode = 0;

    }
    return 0;
}

static int pgsql_out_connect(const struct output_options *options, void * mid_ctx, int startTransaction) {
    global_ctx.middle_ctx = mid_ctx;
    return pgsql_out_connect2(options, global_tables, startTransaction);
}

static int pgsql_out_start(const struct output_options *options)
{
    char *sql, tmp[256];
    PGresult   *res;
    int i,j;
    unsigned int sql_len;
    int their_srid;
    int i_hstore_column;
    enum OsmType type;
    int numTags;
    struct taginfo *exportTags;

    Options = options;

    read_style_file( options->style );

    sql_len = 2048;
    sql = malloc(sql_len);
    assert(sql);

    for (i=0; i<NUM_TABLES; i++) {
        PGconn *sql_conn;

        /* Substitute prefix into name of table */
        {
            char *temp = malloc( strlen(options->prefix) + strlen(global_tables[i].name) + 1 );
            sprintf( temp, global_tables[i].name, options->prefix );
            global_tables[i].name = temp;
        }
        fprintf(stderr, "Setting up table: %s\n", global_tables[i].name);
        sql_conn = PQconnectdb(options->conninfo);

        /* Check to see that the backend connection was successfully made */
        if (PQstatus(sql_conn) != CONNECTION_OK) {
            fprintf(stderr, "Connection to database failed: %s\n", PQerrorMessage(sql_conn));
            exit_nicely();
        }
        global_tables[i].sql_conn = sql_conn;
        pgsql_exec(sql_conn, PGRES_COMMAND_OK, "SET synchronous_commit TO off;");

        if (!options->append) {
            pgsql_exec(sql_conn, PGRES_COMMAND_OK, "DROP TABLE IF EXISTS %s", global_tables[i].name);
        }
        else
        {
            sprintf(sql, "SELECT srid FROM geometry_columns WHERE f_table_name='%s';", global_tables[i].name);
            res = PQexec(sql_conn, sql);
            if (!((PQntuples(res) == 1) && (PQnfields(res) == 1)))
            {
                fprintf(stderr, "Problem reading geometry information for table %s - does it exist?\n", global_tables[i].name);
                exit_nicely();
            }
            their_srid = atoi(PQgetvalue(res, 0, 0));
            PQclear(res);
            if (their_srid != SRID)
            {
                fprintf(stderr, "SRID mismatch: cannot append to table %s (SRID %d) using selected SRID %d\n", global_tables[i].name, their_srid, SRID);
                exit_nicely();
            }
        }

        /* These _tmp tables can be left behind if we run out of disk space */
        pgsql_exec(sql_conn, PGRES_COMMAND_OK, "DROP TABLE IF EXISTS %s_tmp", global_tables[i].name);

        //pgsql_exec(sql_conn, PGRES_COMMAND_OK, "BEGIN");

        type = (i == t_point)?OSMTYPE_NODE:OSMTYPE_WAY;
        numTags = exportListCount[type];
        exportTags = exportList[type];
        if (!options->append) {
            sprintf(sql, "CREATE TABLE %s ( osm_id " POSTGRES_OSMID_TYPE, global_tables[i].name );
            for (j=0; j < numTags; j++) {
                if( exportTags[j].flags & FLAG_DELETE )
                    continue;
                if( (exportTags[j].flags & FLAG_PHSTORE ) == FLAG_PHSTORE)
                    continue;
                sprintf(tmp, ",\"%s\" %s", exportTags[j].name, exportTags[j].type);
                if (strlen(sql) + strlen(tmp) + 1 > sql_len) {
                    sql_len *= 2;
                    sql = realloc(sql, sql_len);
                    assert(sql);
                }
                strcat(sql, tmp);
            }
            for(i_hstore_column = 0; i_hstore_column < Options->n_hstore_columns; i_hstore_column++)
            {
                strcat(sql, ",\"");
                strcat(sql, Options->hstore_columns[i_hstore_column]);
                strcat(sql, "\" hstore ");
            }
            if (Options->enable_hstore) {
                strcat(sql, ",tags hstore");
            } 
            strcat(sql, ")");
            if (Options->tblsmain_data) {
                sprintf(sql + strlen(sql), " TABLESPACE %s", Options->tblsmain_data);
            }
            strcat(sql, "\n");

            pgsql_exec(sql_conn, PGRES_COMMAND_OK, "%s", sql);
            pgsql_exec(sql_conn, PGRES_TUPLES_OK, "SELECT AddGeometryColumn('%s', 'way', %d, '%s', 2 );\n",
                        global_tables[i].name, SRID, global_tables[i].type );
            pgsql_exec(sql_conn, PGRES_COMMAND_OK, "ALTER TABLE %s ALTER COLUMN way SET NOT NULL;\n", global_tables[i].name);
            /* slim mode needs this to be able to apply diffs */
            if (Options->slim && !Options->droptemp) {
                sprintf(sql, "CREATE INDEX %s_pkey ON %s USING BTREE (osm_id)",  global_tables[i].name, global_tables[i].name);
                if (Options->tblsmain_index) {
                    sprintf(sql + strlen(sql), " TABLESPACE %s\n", Options->tblsmain_index);
                }
	            pgsql_exec(sql_conn, PGRES_COMMAND_OK, "%s", sql);
            }
        } else {
            /* Add any new columns referenced in the default.style */
            PGresult *res;
            sprintf(sql, "SELECT * FROM %s LIMIT 0;\n", global_tables[i].name);
            res = PQexec(sql_conn, sql);
            if (PQresultStatus(res) != PGRES_TUPLES_OK) {
                fprintf(stderr, "Error, failed to query table %s\n%s\n", global_tables[i].name, sql);
                exit_nicely();
            }
            for (j=0; j < numTags; j++) {
                if( exportTags[j].flags & FLAG_DELETE )
                    continue;
                if( (exportTags[j].flags & FLAG_PHSTORE) == FLAG_PHSTORE)
                    continue;
                sprintf(tmp, "\"%s\"", exportTags[j].name);
                if (PQfnumber(res, tmp) < 0) {
#if 0
                    fprintf(stderr, "Append failed. Column \"%s\" is missing from \"%s\"\n", exportTags[j].name, tables[i].name);
                    exit_nicely();
#else
                    fprintf(stderr, "Adding new column \"%s\" to \"%s\"\n", exportTags[j].name, global_tables[i].name);
                    pgsql_exec(sql_conn, PGRES_COMMAND_OK, "ALTER TABLE %s ADD COLUMN \"%s\" %s;\n", global_tables[i].name, exportTags[j].name, exportTags[j].type);
#endif
                }
                /* Note: we do not verify the type or delete unused columns */
            }

            PQclear(res);

            /* change the type of the geometry column if needed - this can only change to a more permisive type */
        }
        pgsql_exec(sql_conn, PGRES_COMMAND_OK, "PREPARE get_wkt (" POSTGRES_OSMID_TYPE ") AS SELECT ST_AsText(way) FROM %s WHERE osm_id = $1;\n", global_tables[i].name);
        
        /* Generate column list for COPY */
        strcpy(sql, "osm_id");
        for (j=0; j < numTags; j++) {
            if( exportTags[j].flags & FLAG_DELETE )
                continue;
            if( (exportTags[j].flags & FLAG_PHSTORE ) == FLAG_PHSTORE)
                continue;
            sprintf(tmp, ",\"%s\"", exportTags[j].name);

            if (strlen(sql) + strlen(tmp) + 1 > sql_len) {
                sql_len *= 2;
                sql = realloc(sql, sql_len);
                assert(sql);
            }
            strcat(sql, tmp);
        }

        for(i_hstore_column = 0; i_hstore_column < Options->n_hstore_columns; i_hstore_column++)
        {
            strcat(sql, ",\"");
            strcat(sql, Options->hstore_columns[i_hstore_column]);
            strcat(sql, "\" ");
        }
    
	if (Options->enable_hstore) strcat(sql,",tags");

	global_tables[i].columns = strdup(sql);
        pgsql_exec(sql_conn, PGRES_COPY_IN, "COPY %s (%s,way) FROM STDIN", global_tables[i].name, global_tables[i].columns);

        global_tables[i].copyMode = 1;
    }
    free(sql);

    global_ctx.tables = global_tables;
    global_ctx.geom_ctx = init_geometry_ctx();
    global_ctx.tagtransform_ctx = tagtransform_init(options);

    if (!global_ctx.tagtransform_ctx) {
        fprintf(stderr, "Error: Failed to initialise tag processing.\n");
        exit_nicely();
    }
    expire_tiles_init(options);

    global_ctx.middle_ctx = options->mid->start(options);
    if (global_ctx.middle_ctx == NULL) {
        fprintf(stderr, "Error: Failed to initialise the middle layer.\n");
        exit_nicely();
    }

    return 0;
}

static void pgsql_pause_copy(struct s_table *table)
{
    PGresult   *res;
    int stop;
    
    if( !table->copyMode )
        return;
        
    /* Terminate any pending COPY */
    stop = PQputCopyEnd(table->sql_conn, NULL);
    if (stop != 1) {
       fprintf(stderr, "COPY_END for %s failed: %s\n", table->name, PQerrorMessage(table->sql_conn));
       exit_nicely();
    }

    res = PQgetResult(table->sql_conn);
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
       fprintf(stderr, "COPY_END for %s failed: %s\n", table->name, PQerrorMessage(table->sql_conn));
       PQclear(res);
       exit_nicely();
    }
    PQclear(res);
    table->copyMode = 0;
}

static void pgsql_out_close2(int stopTransaction, struct s_table * tables) {
    int i;
    for (i=0; i<NUM_TABLES; i++) {
        pgsql_pause_copy(&tables[i]);
        /* Commit transaction */
        if (stopTransaction)
            pgsql_exec(tables[i].sql_conn, PGRES_COMMAND_OK, "COMMIT");
        PQfinish(tables[i].sql_conn);
        tables[i].sql_conn = NULL;
    }
}

static void pgsql_out_close(int stopTransaction) {
    pgsql_out_close2(stopTransaction, global_tables);
}

static void pgsql_out_commit(void) {
    int i;
    for (i=0; i<NUM_TABLES; i++) {
        pgsql_pause_copy(&global_tables[i]);
        /* Commit transaction */
        fprintf(stderr, "Committing transaction for %s\n", global_tables[i].name);
        pgsql_exec(global_tables[i].sql_conn, PGRES_COMMAND_OK, "COMMIT");
    }
}

static void *pgsql_out_stop_one(void *arg)
{
    int i_column;
    struct s_table *table = arg;
    PGconn *sql_conn = table->sql_conn;

    if( table->buflen != 0 )
    {
       fprintf( stderr, "Internal error: Buffer for %s has %d bytes after end copy", table->name, table->buflen );
       exit_nicely();
    }

    pgsql_pause_copy(table);
    if (!Options->append)
    {
        time_t start, end;
        time(&start);
        fprintf(stderr, "Sorting data and creating indexes for %s\n", table->name);
        pgsql_exec(sql_conn, PGRES_COMMAND_OK, "ANALYZE %s;\n", table->name);
        fprintf(stderr, "Analyzing %s finished\n", table->name);
        if (Options->tblsmain_data) {
            pgsql_exec(sql_conn, PGRES_COMMAND_OK, "CREATE TABLE %s_tmp "
                        "TABLESPACE %s AS SELECT * FROM %s ORDER BY way;\n",
                        table->name, Options->tblsmain_data, table->name);
        } else {
            pgsql_exec(sql_conn, PGRES_COMMAND_OK, "CREATE TABLE %s_tmp AS SELECT * FROM %s ORDER BY way;\n", table->name, table->name);
        }
        pgsql_exec(sql_conn, PGRES_COMMAND_OK, "DROP TABLE %s;\n", table->name);
        pgsql_exec(sql_conn, PGRES_COMMAND_OK, "ALTER TABLE %s_tmp RENAME TO %s;\n", table->name, table->name);
        fprintf(stderr, "Copying %s to cluster by geometry finished\n", table->name);
        fprintf(stderr, "Creating geometry index on  %s\n", table->name);
        if (Options->tblsmain_index) {
            /* Use fillfactor 100 for un-updatable imports */
            if (Options->slim && !Options->droptemp) {
                pgsql_exec(sql_conn, PGRES_COMMAND_OK, "CREATE INDEX %s_index ON %s USING GIST (way) TABLESPACE %s;\n", table->name, table->name, Options->tblsmain_index);
            } else {
                pgsql_exec(sql_conn, PGRES_COMMAND_OK, "CREATE INDEX %s_index ON %s USING GIST (way) WITH (FILLFACTOR=100) TABLESPACE %s;\n", table->name, table->name, Options->tblsmain_index);
            }
        } else {
            if (Options->slim && !Options->droptemp) {
                pgsql_exec(sql_conn, PGRES_COMMAND_OK, "CREATE INDEX %s_index ON %s USING GIST (way);\n", table->name, table->name);
            } else {
                pgsql_exec(sql_conn, PGRES_COMMAND_OK, "CREATE INDEX %s_index ON %s USING GIST (way) WITH (FILLFACTOR=100);\n", table->name, table->name);
            }
        }

        /* slim mode needs this to be able to apply diffs */
        if (Options->slim && !Options->droptemp)
        {
            fprintf(stderr, "Creating osm_id index on  %s\n", table->name);
            if (Options->tblsmain_index) {
                pgsql_exec(sql_conn, PGRES_COMMAND_OK, "CREATE INDEX %s_pkey ON %s USING BTREE (osm_id) TABLESPACE %s;\n", table->name, table->name, Options->tblsmain_index);
            } else {
                pgsql_exec(sql_conn, PGRES_COMMAND_OK, "CREATE INDEX %s_pkey ON %s USING BTREE (osm_id);\n", table->name, table->name);
            }
        }
        /* Create hstore index if selected */
        if (Options->enable_hstore_index) {
            fprintf(stderr, "Creating hstore indexes on  %s\n", table->name);
            if (Options->tblsmain_index) {
                if (HSTORE_NONE != (Options->enable_hstore)) {
                    if (Options->slim && !Options->droptemp) {
                        pgsql_exec(sql_conn, PGRES_COMMAND_OK, "CREATE INDEX %s_tags_index ON %s USING GIN (tags) TABLESPACE %s;\n", table->name, table->name, Options->tblsmain_index);
                    } else {
                        pgsql_exec(sql_conn, PGRES_COMMAND_OK, "CREATE INDEX %s_tags_index ON %s USING GIN (tags) WITH (FILLFACTOR=100) TABLESPACE %s;\n", table->name, table->name, Options->tblsmain_index);
                    }
                }
                for(i_column = 0; i_column < Options->n_hstore_columns; i_column++) {
                    if (Options->slim && !Options->droptemp) {
                        pgsql_exec(sql_conn, PGRES_COMMAND_OK, "CREATE INDEX %s_hstore_%i_index ON %s USING GIN (\"%s\") TABLESPACE %s;\n",
                               table->name, i_column,table->name, Options->hstore_columns[i_column], Options->tblsmain_index);
                    } else {
                        pgsql_exec(sql_conn, PGRES_COMMAND_OK, "CREATE INDEX %s_hstore_%i_index ON %s USING GIN (\"%s\") WITH (FILLFACTOR=100) TABLESPACE %s;\n",
                               table->name, i_column,table->name, Options->hstore_columns[i_column], Options->tblsmain_index);
                    }
                }
            } else {
                if (HSTORE_NONE != (Options->enable_hstore)) {
                    if (Options->slim && !Options->droptemp) {
                        pgsql_exec(sql_conn, PGRES_COMMAND_OK, "CREATE INDEX %s_tags_index ON %s USING GIN (tags);\n", table->name, table->name);
                    } else {
                        pgsql_exec(sql_conn, PGRES_COMMAND_OK, "CREATE INDEX %s_tags_index ON %s USING GIN (tags) WITH (FILLFACTOR=100);\n", table->name, table->name);
                    }
                }
                for(i_column = 0; i_column < Options->n_hstore_columns; i_column++) {
                    if (Options->slim && !Options->droptemp) {
                        pgsql_exec(sql_conn, PGRES_COMMAND_OK, "CREATE INDEX %s_hstore_%i_index ON %s USING GIN (\"%s\");\n", table->name, i_column,table->name, Options->hstore_columns[i_column]);
                    } else {
                        pgsql_exec(sql_conn, PGRES_COMMAND_OK, "CREATE INDEX %s_hstore_%i_index ON %s USING GIN (\"%s\") WITH (FILLFACTOR=100);\n", table->name, i_column,table->name, Options->hstore_columns[i_column]);
                    }
                }
            }
        }
        fprintf(stderr, "Creating indexes on  %s finished\n", table->name);
        pgsql_exec(sql_conn, PGRES_COMMAND_OK, "GRANT SELECT ON %s TO PUBLIC;\n", table->name);
        pgsql_exec(sql_conn, PGRES_COMMAND_OK, "ANALYZE %s;\n", table->name);
        time(&end);
        fprintf(stderr, "All indexes on  %s created  in %ds\n", table->name, (int)(end - start));
    }
    PQfinish(sql_conn);
    table->sql_conn = NULL;

    fprintf(stderr, "Completed %s\n", table->name);
    free(table->name);
    free(table->columns);
    return NULL;
}

static void pgsql_out_stop()
{
    int i;
#ifdef HAVE_PTHREAD
    pthread_t threads[NUM_TABLES];
#endif

    /* Commit the transactions, so that multiple processes can
     * access the data simultanious to process the rest in parallel
     * as well as see the newly created tables.
     */
    //pgsql_out_commit();

#ifdef HAVE_PTHREAD
    workers_finish = 1;
    pthread_cond_broadcast(&cond_worker_queue_work_available);
    for (i=0; i<Options->num_procs; i++) {
        int ret = pthread_join(worker_threads[i], NULL);
        if (ret) {
            fprintf(stderr, "pthread_join() returned an error (%d)", ret);
            exit_nicely();
        }
    }
    free(worker_threads);
    worker_threads = NULL;
#endif


    Options->mid->commit(global_ctx.middle_ctx);
    /* To prevent deadlocks in parallel processing, the mid tables need
     * to stay out of a transaction. In this stage output tables are only
     * written to and not read, so they can be processed as several parallel
     * independent transactions
     */
    /*for (i=0; i<NUM_TABLES; i++) {
        PGconn *sql_conn = global_tables[i].sql_conn;
        pgsql_exec(sql_conn, PGRES_COMMAND_OK, "BEGIN");
    }*/
    /* Processing any remaining to be processed ways */
    Options->mid->iterate_ways( pgsql_out_way );
    //pgsql_out_commit();
    Options->mid->commit(global_ctx.middle_ctx);

    /* Processing any remaing to be processed relations */
    /* During this stage output tables also need to stay out of
     * extended transactions, as the delete_way_from_output, called
     * from process_relation, can deadlock if using multi-processing.
     */    
    Options->mid->iterate_relations( pgsql_process_relation );

    tagtransform_shutdown(global_ctx.tagtransform_ctx);
    close_geometry_ctx(global_ctx.geom_ctx);
    global_ctx.geom_ctx = NULL;

#ifdef HAVE_PTHREAD
    if (Options->parallel_indexing) {
      for (i=0; i<NUM_TABLES; i++) {
          int ret = pthread_create(&threads[i], NULL, pgsql_out_stop_one, &global_tables[i]);
          if (ret) {
              fprintf(stderr, "pthread_create() returned an error (%d)", ret);
              exit_nicely();
          }
      }
  
      /* No longer need to access middle layer -- release memory */
      Options->mid->stop();
  
      for (i=0; i<NUM_TABLES; i++) {
          int ret = pthread_join(threads[i], NULL);
          if (ret) {
              fprintf(stderr, "pthread_join() returned an error (%d)", ret);
              exit_nicely();
          }
      }
    } else {
#endif

    /* No longer need to access middle layer -- release memory */
    Options->mid->stop();
    for (i=0; i<NUM_TABLES; i++)
        pgsql_out_stop_one(&global_tables[i]);

#ifdef HAVE_PTHREAD
    }
#endif


    pgsql_out_cleanup(global_tables);
    free_style();

    expire_tiles_stop();
}

static int pgsql_add_node(osmid_t id, double lat, double lon, struct keyval *tags)
{
  Options->mid->nodes_set(global_ctx.middle_ctx, id, lat, lon, tags);
  pgsql_out_node(id, tags, lat, lon);

  return 0;
}



static int pgsql_add_way(osmid_t id, osmid_t *nds, int nd_count, struct keyval *tags){
    struct way_info2 * way = malloc(sizeof(struct way_info2));
    int i;

#ifdef HAVE_PTHREAD
    if (!worker_threads) {
        Options->mid->commit(global_ctx.middle_ctx);
        pgsql_pause_copy(&global_tables[t_point]);
        pgsql_pause_copy(&global_tables[t_line]);
        pgsql_pause_copy(&global_tables[t_roads]);
        pgsql_pause_copy(&global_tables[t_poly]);
        worker_threads = calloc(Options->num_procs, sizeof(pthread_t));
        for (i = 0; i < Options->num_procs; i++) {
            int * thread_id = malloc(sizeof(int));
            *thread_id = i + 1; //thread_id is the global_ctx thread.
            int ret = pthread_create(&(worker_threads[i]), NULL,
                    &pgsql_worker_thread, thread_id );
            if (ret) {
                fprintf(stderr, "pthread_create() returned an error (%d)",
                        ret);
                exit_nicely();
            }
        }
    }
#endif //HAVE_PTHREAD

    way->id = id;
    way->nd_count = nd_count;
    way->nds = malloc(sizeof(osmid_t) * nd_count);
    memcpy(way->nds, nds, sizeof(osmid_t) * nd_count);
    way->tags = malloc(sizeof(struct keyval));
    initList(way->tags);
    cloneList(way->tags, tags);

#ifdef HAVE_PTHREAD
    pthread_mutex_lock(&lock_worker_queue);
    while ((ways_buffer_pfree + 1) % WORKER_THREAD_QUEUE_SIZE == ways_buffer_pfirst) {
        //Queue is full, wait until the worker threads have processed some of it
        //and there is space in the queue again.
        pthread_cond_wait(&cond_worker_queue_space_available, &lock_worker_queue);
    }

    ways_buffer[ways_buffer_pfree] = way;
    ways_buffer_pfree++;
    if (ways_buffer_pfree > (WORKER_THREAD_QUEUE_SIZE - 1))
        ways_buffer_pfree = 0;
    pthread_mutex_unlock(&lock_worker_queue);
    pthread_cond_signal(&cond_worker_queue_work_available);
    return 0;
#else
    pgsql_add_way_single(&global_ctx, way);
#endif

}





static int pgsql_process_relation(osmid_t id, struct member *members, int member_count, struct keyval *tags, int exists) {
    int i;
    struct relation_info2 * rel = malloc(sizeof(struct relation_info2));

    rel->id = id;
    rel->tags = malloc(sizeof(struct keyval));
    initList(rel->tags);
    cloneList(rel->tags, tags);
    rel->exists = exists;
    rel->member_count = member_count;
    rel->members = malloc(sizeof(struct member) * member_count);
    for (i = 0; i < member_count; i++) {
        rel->members[i].id = members[i].id;
        rel->members[i].type = members[i].type;
        rel->members[i].role = strdup(members[i].role);
    }

#ifdef HAVE_PTHREAD
    if (workers_finish == 0) {
        pthread_mutex_lock(&lock_worker_queue);
        while ((rels_buffer_pfree + 1) % WORKER_THREAD_QUEUE_SIZE == rels_buffer_pfirst ) {
            pthread_cond_wait(&cond_worker_queue_space_available, &lock_worker_queue);
        }

        rels_buffer[rels_buffer_pfree] = rel;
        rels_buffer_pfree++;
        if (rels_buffer_pfree > (WORKER_THREAD_QUEUE_SIZE - 1)) rels_buffer_pfree = 0;
        pthread_mutex_unlock(&lock_worker_queue);
        pthread_cond_signal(&cond_worker_queue_work_available);
        return 0;
    } else
#endif
        return pgsql_process_relation_single(&global_ctx, rel);
}

static int pgsql_add_relation(osmid_t id, struct member *members, int member_count, struct keyval *tags)
{
  const char *type = getItem(tags, "type");

  /* Must have a type field or we ignore it */
  if (!type)
      return 0;

  /* In slim mode we remember these */
  if(Options->mid->relations_set)
    Options->mid->relations_set(global_ctx.middle_ctx, id, members, member_count, tags);
    
  /* Only a limited subset of type= is supported, ignore other */
  if ( (strcmp(type, "route") != 0) && (strcmp(type, "multipolygon") != 0) && (strcmp(type, "boundary") != 0))
    return 0;


  return pgsql_process_relation(id, members, member_count, tags, 0);
}
#define UNUSED  __attribute__ ((unused))

/* Delete is easy, just remove all traces of this object. We don't need to
 * worry about finding objects that depend on it, since the same diff must
 * contain the change for that also. */
static int pgsql_delete_node(osmid_t osm_id)
{
    if( !Options->slim )
    {
        fprintf( stderr, "Cannot apply diffs unless in slim mode\n" );
        exit_nicely();
    }
    pgsql_pause_copy(&global_tables[t_point]);
    if ( expire_tiles_from_db(global_tables[t_point].sql_conn, osm_id) != 0)
        pgsql_exec(global_tables[t_point].sql_conn, PGRES_COMMAND_OK, "DELETE FROM %s WHERE osm_id = %" PRIdOSMID, global_tables[t_point].name, osm_id );
    
    Options->mid->nodes_delete(global_ctx.middle_ctx, osm_id);
    return 0;
}

/* Seperated out because we use it elsewhere */
static int pgsql_delete_way_from_output(osmid_t osm_id, struct s_table * tables)
{
    /* Optimisation: we only need this is slim mode */
    if( !Options->slim )
        return 0;
    /* in droptemp mode we don't have indices and this takes ages. */
    if (Options->droptemp)
        return 0;
    pgsql_pause_copy(&(tables[t_roads]));
    pgsql_pause_copy(&(tables[t_line]));
    pgsql_pause_copy(&(tables[t_poly]));
    pgsql_exec(tables[t_roads].sql_conn, PGRES_COMMAND_OK, "DELETE FROM %s WHERE osm_id = %" PRIdOSMID, tables[t_roads].name, osm_id );
    if ( expire_tiles_from_db(tables[t_line].sql_conn, osm_id) != 0)
        pgsql_exec(tables[t_line].sql_conn, PGRES_COMMAND_OK, "DELETE FROM %s WHERE osm_id = %" PRIdOSMID, tables[t_line].name, osm_id );
    if ( expire_tiles_from_db(tables[t_poly].sql_conn, osm_id) != 0)
        pgsql_exec(tables[t_poly].sql_conn, PGRES_COMMAND_OK, "DELETE FROM %s WHERE osm_id = %" PRIdOSMID, tables[t_poly].name, osm_id );
    return 0;
}

static int pgsql_delete_way(osmid_t osm_id)
{
    if( !Options->slim )
    {
        fprintf( stderr, "Cannot apply diffs unless in slim mode\n" );
        exit_nicely();
    }
    pgsql_delete_way_from_output(osm_id, global_tables);
    Options->mid->ways_delete(global_ctx.middle_ctx, osm_id);
    return 0;
}

/* Relations are identified by using negative IDs */
static int pgsql_delete_relation_from_output(osmid_t osm_id)
{
    pgsql_pause_copy(&global_tables[t_roads]);
    pgsql_pause_copy(&global_tables[t_line]);
    pgsql_pause_copy(&global_tables[t_poly]);
    pgsql_exec(global_tables[t_roads].sql_conn, PGRES_COMMAND_OK, "DELETE FROM %s WHERE osm_id = %" PRIdOSMID, global_tables[t_roads].name, -osm_id );
    if ( expire_tiles_from_db(global_tables[t_line].sql_conn, -osm_id) != 0)
        pgsql_exec(global_tables[t_line].sql_conn, PGRES_COMMAND_OK, "DELETE FROM %s WHERE osm_id = %" PRIdOSMID, global_tables[t_line].name, -osm_id );
    if ( expire_tiles_from_db(global_tables[t_poly].sql_conn, -osm_id) != 0)
        pgsql_exec(global_tables[t_poly].sql_conn, PGRES_COMMAND_OK, "DELETE FROM %s WHERE osm_id = %" PRIdOSMID, global_tables[t_poly].name, -osm_id );
    return 0;
}

static int pgsql_delete_relation(osmid_t osm_id)
{
    if( !Options->slim )
    {
        fprintf( stderr, "Cannot apply diffs unless in slim mode\n" );
        exit_nicely();
    }
    pgsql_delete_relation_from_output(osm_id);
    Options->mid->relations_delete(global_ctx.middle_ctx, osm_id);
    return 0;
}

/* Modify is slightly trickier. The basic idea is we simply delete the
 * object and create it with the new parameters. Then we need to mark the
 * objects that depend on this one */
static int pgsql_modify_node(osmid_t osm_id, double lat, double lon, struct keyval *tags)
{
    if( !Options->slim )
    {
        fprintf( stderr, "Cannot apply diffs unless in slim mode\n" );
        exit_nicely();
    }
    pgsql_delete_node(osm_id);
    pgsql_add_node(osm_id, lat, lon, tags);
    Options->mid->node_changed(global_ctx.middle_ctx, osm_id);
    return 0;
}

static int pgsql_modify_way(osmid_t osm_id, osmid_t *nodes, int node_count, struct keyval *tags)
{
    if( !Options->slim )
    {
        fprintf( stderr, "Cannot apply diffs unless in slim mode\n" );
        exit_nicely();
    }
    pgsql_delete_way(osm_id);
    pgsql_add_way(osm_id, nodes, node_count, tags);
    Options->mid->way_changed(global_ctx.middle_ctx, osm_id);
    return 0;
}

static int pgsql_modify_relation(osmid_t osm_id, struct member *members, int member_count, struct keyval *tags)
{
    if( !Options->slim )
    {
        fprintf( stderr, "Cannot apply diffs unless in slim mode\n" );
        exit_nicely();
    }
    pgsql_delete_relation(osm_id);
    pgsql_add_relation(osm_id, members, member_count, tags);
    Options->mid->relation_changed(global_ctx.middle_ctx, osm_id);
    return 0;
}

struct output_t out_pgsql = {
        .start           = pgsql_out_start,
        .connect         = pgsql_out_connect,
        .stop            = pgsql_out_stop,
        .cleanup         = pgsql_out_cleanup,
        .close           = pgsql_out_close,
        .node_add        = pgsql_add_node,
        .way_add         = pgsql_add_way,
        .relation_add    = pgsql_add_relation,
        
        .node_modify     = pgsql_modify_node,
        .way_modify      = pgsql_modify_way,
        .relation_modify = pgsql_modify_relation,

        .node_delete     = pgsql_delete_node,
        .way_delete      = pgsql_delete_way,
        .relation_delete = pgsql_delete_relation
};
