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
#include <errno.h>

#include <libpq-fe.h>

#include "osmtypes.h"
#include "output.h"
#include "reprojection.h"
#include "output-pgsql.h"
#include "build_geometry.h"
#include "middle.h"
#include "pgsql.h"

#define SRID (project_getprojinfo()->srs)

#define MAX_STYLES 100

enum table_id {
    t_point, t_line, t_poly, t_roads
};

static const struct output_options *Options;

/* Tables to output */
static struct {
    //enum table_id table;
    const char *name;
    const char *type;
} tables [] = {
    { name: "%s_point",   type: "POINT"     },
    { name: "%s_line",    type: "LINESTRING"},
    { name: "%s_polygon", type: "POLYGON"  },
    { name: "%s_roads",   type: "LINESTRING"}
};
static const int num_tables = sizeof(tables)/sizeof(*tables);

/* Table columns, representing key= tags */
struct taginfo {
    char *name;
    char *type;
    int polygon;
    int count;
};

static struct taginfo *exportList[4]; /* Indexed by enum table_id */
static int exportListCount[4];

/* Data to generate z-order column and road table */
static struct {
    int offset;
    const char *highway;
    int roads;
} layers[] = {
    { 9, "motorway",      1 },
    { 9, "motorway_link", 1 },
    { 8, "trunk",         1 },
    { 8, "trunk_link",    1 },
    { 7, "primary",       1 },
    { 7, "primary_link",  1 },
    { 6, "secondary",     1 },
    { 6, "secondary_link",1 },
   // 5 = railway
    { 4, "tertiary",      0 },
    { 4, "tertiary_link", 0 },
    { 3, "residential",   0 },
    { 3, "unclassified",  0 },
    { 3, "minor",         0 }
};
static const unsigned int nLayers = (sizeof(layers)/sizeof(*layers));


static PGconn **sql_conns;

void read_style_file( char *filename )
{
  FILE *in;
  int lineno = 0;
  
  exportList[OSMTYPE_NODE] = malloc( sizeof(struct taginfo) * MAX_STYLES );
  exportList[OSMTYPE_WAY]  = malloc( sizeof(struct taginfo) * MAX_STYLES );
  
  in = fopen( filename, "rt" );
  if( !in )
  {
    fprintf( stderr, "Couldn't open style file '%s': %s\n", filename, strerror(errno) );
    exit_nicely();
  }
  
  char buffer[1024];
  while( fgets( buffer, sizeof(buffer), in) != NULL )
  {
    lineno++;
    
    char osmtype[24];
    char tag[24];
    char datatype[24];
    int offset = 0;

    if( buffer[0] == '#' )
      continue;
      
    int fields = sscanf( buffer, "%s %s %s %n", osmtype, tag, datatype, &offset );
    if( fields <= 0 )  /* Blank line */
      continue;
    if( fields < 3 )
    {
      fprintf( stderr, "Error reading style file line %d (fields=%d)\n", lineno, fields );
      exit_nicely();
    }
    struct taginfo temp;
    temp.name = strdup( tag );
    temp.type = strdup(datatype);
    temp.polygon = ( strstr( buffer+offset, "polygon" ) != NULL );
    temp.count = 0;
//    printf("%s %s %d %d\n", temp.name, temp.type, temp.polygon, offset );
    
    int flag = 0;
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
  }
  fclose(in);
}


void sql_out(PGconn *sql_conn, const char *sql, int len)
{
    unsigned int r;

    r = PQputCopyData(sql_conn, sql, len);
    if (r != 1) {
        fprintf(stderr, "bad result %d, line '%.*s'\n", r, len, sql);
        exit_nicely();
    }
}

static int add_z_order_polygon(struct keyval *tags, int *roads)
{
    const char *natural = getItem(tags, "natural");
    const char *layer   = getItem(tags, "layer");
#if 0
    const char *landuse = getItem(tags, "landuse");
    const char *leisure = getItem(tags, "leisure");
#endif
    int z_order, l;
    char z[13];

    /* Discard polygons with the tag natural=coastline */
    if (natural && !strcmp(natural, "coastline"))
        return 1;

    l = layer ? strtol(layer, NULL, 10) : 0;
    z_order = 10 * l;
    *roads = 0;
#if 0
    /* - New scheme uses layer + way area to control render order, not tags */

    /* landuse & leisure tend to cover large areas and we want them under other polygons */
    if (landuse) z_order -= 2;
    if (leisure) z_order -= 1;
#endif
    snprintf(z, sizeof(z), "%d", z_order);
    addItem(tags, "z_order", z, 0);

    return 0;
}


static int add_z_order_line(struct keyval *tags, int *roads)
{
    const char *layer   = getItem(tags, "layer");
    const char *highway = getItem(tags, "highway");
    const char *bridge  = getItem(tags, "bridge");
    const char *tunnel  = getItem(tags, "tunnel");
    const char *railway = getItem(tags, "railway");
    int z_order = 0;
    int l;
    unsigned int i;
    char z[13];

    l = layer ? strtol(layer, NULL, 10) : 0;
    z_order = 10 * l;
    *roads = 0;

    if (highway) {
        for (i=0; i<nLayers; i++) {
            if (!strcmp(layers[i].highway, highway)) {
                z_order += layers[i].offset;
                *roads   = layers[i].roads;
                break;
            }
        }
    }

    if (railway && strlen(railway)) {
        z_order += 5;
        *roads = 1;
    }

    if (bridge && (!strcmp(bridge, "true") || !strcmp(bridge, "yes") || !strcmp(bridge, "1")))
        z_order += 10;

    if (tunnel && (!strcmp(tunnel, "true") || !strcmp(tunnel, "yes") || !strcmp(tunnel, "1")))
        z_order -= 10;

    snprintf(z, sizeof(z), "%d", z_order);
    addItem(tags, "z_order", z, 0);

    return 0;
}

static int add_z_order(struct keyval* tags, int polygon, int *roads)
{
    return polygon ? add_z_order_polygon(tags, roads) : add_z_order_line(tags, roads);
}


static void fix_motorway_shields(struct keyval *tags)
{
    const char *highway = getItem(tags, "highway");
    const char *name    = getItem(tags, "name");
    const char *ref     = getItem(tags, "ref");

    /* The current mapnik style uses ref= for motorway shields but some roads just have name= */
    if (!highway || strcmp(highway, "motorway"))
        return;

    if (name && !ref)
        addItem(tags, "ref", name, 0);
}


/* Append all alternate name:xx on to the name tag with space sepatators.
 * name= always comes first, the alternates are in no particular order
 * Note: A new line may be better but this does not work with Mapnik
 * 
 *    <tag k="name" v="Ben Nevis" />
 *    <tag k="name:gd" v="Ben Nibheis" />
 * becomes:
 *    <tag k="name" v="Ben Nevis Ben Nibheis" />
 */
void compress_tag_name(struct keyval *tags)
{
    const char *name = getItem(tags, "name");
    struct keyval *name_ext = getMatches(tags, "name:");
    struct keyval *p;
    char out[2048];

    if (!name_ext)
        return;

    out[0] = '\0';
    if (name) {
        strncat(out, name, sizeof(out)-1);
        strncat(out, " ", sizeof(out)-1);
    }
    while((p = popItem(name_ext)) != NULL) {
        /* Exclude name:source = "dicataphone" and duplicates */
        if (strcmp(p->key, "name:source") && !strstr(out, p->value)) {
            strncat(out, p->value, sizeof(out)-1);
            strncat(out, " ", sizeof(out)-1);
        }
        freeItem(p);
    }
    free(name_ext);

    // Remove trailing space
    out[strlen(out)-1] = '\0';
    //fprintf(stderr, "*** New name: %s\n", out);
    updateItem(tags, "name", out);
}



static void pgsql_out_cleanup(void)
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
    free(sql_conns);
    sql_conns = NULL;
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

static int pgsql_out_node(int id, struct keyval *tags, double node_lat, double node_lon)
{
    char sql[2048], *v;
    int i, export = 0;
    PGconn *sql_conn = sql_conns[t_point];

    for (i=0; i < exportListCount[OSMTYPE_NODE]; i++) {
        if ((v = getItem(tags, exportList[OSMTYPE_NODE][i].name))) {
            export = 1;
            break;
        }
    }

    if (!export)
       return 0;

    //compress_tag_name(tags);

    sprintf(sql, "%d\t", id);
    sql_out(sql_conn, sql, strlen(sql));

    for (i=0; i < exportListCount[OSMTYPE_NODE]; i++) {
        if ((v = getItem(tags, exportList[OSMTYPE_NODE][i].name)))
        {
            escape(sql, sizeof(sql), v);
            exportList[OSMTYPE_NODE][i].count++;
        }
        else
            sprintf(sql, "\\N");

        sql_out(sql_conn, sql, strlen(sql));
        sql_out(sql_conn, "\t", 1);
    }

    sprintf(sql, "SRID=%d;POINT(%.15g %.15g)", SRID, node_lon, node_lat);
    sql_out(sql_conn, sql, strlen(sql));
    sql_out(sql_conn, "\n", 1);

    return 0;
}



static void write_wkts(int id, struct keyval *tags, const char *wkt, PGconn *sql_conn)
{
    int j;
    char sql[2048];
    const char*v;

    sprintf(sql, "%d\t", id);
    sql_out(sql_conn, sql, strlen(sql));

    for (j=0; j < exportListCount[OSMTYPE_WAY]; j++) {
	    if ((v = getItem(tags, exportList[OSMTYPE_WAY][j].name)))
	    {
	            exportList[OSMTYPE_WAY][j].count++;
		    escape(sql, sizeof(sql), v);
            }
	    else
		    sprintf(sql, "\\N");

	    sql_out(sql_conn, sql, strlen(sql));
	    sql_out(sql_conn, "\t", 1);
    }

    sprintf(sql, "SRID=%d;", SRID);
    sql_out(sql_conn, sql, strlen(sql));
    sql_out(sql_conn, wkt, strlen(wkt));
    sql_out(sql_conn, "\n", 1);
}

void add_parking_node(int id, struct keyval *tags, double node_lat, double node_lon)
{
// insert into planet_osm_point(osm_id,name,amenity,way) select osm_id,name,amenity,centroid(way) from planet_osm_polygon where amenity='parking';
	const char *access  = getItem(tags, "access");
	const char *amenity = getItem(tags, "amenity");
	const char *name    = getItem(tags, "name");
	struct keyval nodeTags;
	
	if (!amenity || strcmp(amenity, "parking"))
		return;

	// Do not add a 'P' symbol if access is defined and something other than public.
	if (access && strcmp(access, "public"))
		return;

	initList(&nodeTags);
	addItem(&nodeTags, "amenity", amenity, 0);
	if (name)
		addItem(&nodeTags, "name",    name,    0);
	if (access)
		addItem(&nodeTags, "access",  access,    0);
	
	pgsql_out_node(id, &nodeTags, node_lat, node_lon);
	resetList(&nodeTags);
}


unsigned int pgsql_filter_tags(enum OsmType type, struct keyval *tags, int *polygon)
{
    const char *v;
    int i, filter = 1;

    *polygon = 0;

    for (i=0; i < exportListCount[type]; i++) {
        if ((v = getItem(tags, exportList[type][i].name))) {
            filter = 0;
            *polygon |= exportList[type][i].polygon;
            if (*polygon)
                break;
        }
    }

    return filter;
}

/*
COPY planet_osm (osm_id, name, place, landuse, leisure, "natural", man_made, waterway, highway, railway, amenity, tourism, learning, bu
ilding, bridge, layer, way) FROM stdin;
198497  Bedford Road    \N      \N      \N      \N      \N      \N      residential     \N      \N      \N      \N      \N      \N    \N       0102000020E610000004000000452BF702B342D5BF1C60E63BF8DF49406B9C4D470037D5BF5471E316F3DF4940DFA815A6EF35D5BF9AE95E27F5DF4940B41EB
E4C1421D5BF24D06053E7DF4940
212696  Oswald Road     \N      \N      \N      \N      \N      \N      minor   \N      \N      \N      \N      \N      \N      \N    0102000020E610000004000000467D923B6C22D5BFA359D93EE4DF4940B3976DA7AD11D5BF84BBB376DBDF4940997FF44D9A06D5BF4223D8B8FEDF49404D158C4AEA04D
5BF5BB39597FCDF4940
*/
static int pgsql_out_way(int id, struct keyval *tags, struct osmNode *nodes, int count)
{
    int polygon = 0, roads = 0;
    char *wkt;
    double area, interior_lat, interior_lon;

    if (pgsql_filter_tags(OSMTYPE_WAY, tags, &polygon) || add_z_order(tags, polygon, &roads))
        return 0;

    //compress_tag_name(tags);

    fix_motorway_shields(tags);

    wkt = get_wkt_simple(nodes, count, polygon, &area, &interior_lon, &interior_lat);
    if (wkt && strlen(wkt)) {
	/* FIXME: there should be a better way to detect polygons */
	if (!strncmp(wkt, "POLYGON", strlen("POLYGON"))) {
	    if (area > 0.0) {
		char tmp[32];
		snprintf(tmp, sizeof(tmp), "%f", area);
		addItem(tags, "way_area", tmp, 0);
	    }
	    write_wkts(id, tags, wkt, sql_conns[t_poly]);
	    add_parking_node(id, tags, interior_lat, interior_lon);
	} else {
	    write_wkts(id, tags, wkt, sql_conns[t_line]);
	    if (roads)
		write_wkts(id, tags, wkt, sql_conns[t_roads]);
	}
    }
    free(wkt);
	
    return 0;
}

static int pgsql_out_relation(int id, struct member *members, int member_count, struct keyval *rel_tags, struct osmNode **xnodes, struct keyval *xtags, int *xcount)
{
    int i, wkt_size;
    double interior_lat, interior_lon;
    int polygon = 0, roads = 0;
    struct keyval tags, *p;
#if 0
    fprintf(stderr, "Got relation with counts:");
    for (i=0; xcount[i]; i++)
        fprintf(stderr, " %d", xcount[i]);
    fprintf(stderr, "\n");
#endif
    // Aggregate tags from all polygons with the relation,
    // no idea is this is a good way to deal with differing tags across the ways or not
    initList(&tags);
    p = rel_tags->next;
    while (p != rel_tags) {
        addItem(&tags, p->key, p->value, 1);
        p = p->next;
    }
    for (i=0; xcount[i]; i++) {
        p = xtags[i].next;
        while (p != &(xtags[i])) {
            addItem(&tags, p->key, p->value, 1);
            p = p->next;
        }
    }

    if (pgsql_filter_tags(OSMTYPE_WAY, &tags, &polygon) || add_z_order(&tags, polygon, &roads)) {
        resetList(&tags);
        return 0;
    }

    wkt_size = build_geometry(id, xnodes, xcount);

    if (!wkt_size) {
        resetList(&tags);
        return 0;
    }

    for (i=0;i<wkt_size;i++)
    {
        char *wkt = get_wkt(i);

        if (strlen(wkt)) {
            /* FIXME: there should be a better way to detect polygons */
            if (!strncmp(wkt, "POLYGON", strlen("POLYGON"))) {
                double area = get_area(i);
                if (area > 0.0) {
                    char tmp[32];
                    snprintf(tmp, sizeof(tmp), "%f", area);
                    addItem(&tags, "way_area", tmp, 0);
                }
                write_wkts(id, &tags, wkt, sql_conns[t_poly]);
                get_interior(i, &interior_lat, &interior_lon);
                add_parking_node(id, &tags, interior_lat, interior_lon);
	    } else {
                write_wkts(id, &tags, wkt, sql_conns[t_line]);
                if (roads)
                    write_wkts(id, &tags, wkt, sql_conns[t_roads]);
            }
	}
        free(wkt);
    }
	
    clear_wkts();

    // Mark each member so that we can skip them during iterate_ways
    for (i=0; i<member_count; i++)
        if( members[i].type == OSMTYPE_WAY )
            Options->mid->ways_done(members[i].id);

    resetList(&tags);
    return 0;
}

static int pgsql_out_start(const struct output_options *options)
{
    char sql[1024], tmp[128];
    PGresult   *res;
    int i,j;

    Options = options;
    /* We use a connection per table to enable the use of COPY_IN */
    sql_conns = calloc(num_tables, sizeof(PGconn *));
    assert(sql_conns);
    
    read_style_file( "default.style" );

    for (i=0; i<num_tables; i++) {
        PGconn *sql_conn;

        /* Substitute prefix into name of table */
        {
            char *temp = malloc( strlen(options->prefix) + strlen(tables[i].name) + 1 );
            sprintf( temp, tables[i].name, options->prefix );
            tables[i].name = temp;
        }
        fprintf(stderr, "Setting up table: %s\n", tables[i].name);
        sql_conn = PQconnectdb(options->conninfo);

        /* Check to see that the backend connection was successfully made */
        if (PQstatus(sql_conn) != CONNECTION_OK) {
            fprintf(stderr, "Connection to database failed: %s\n", PQerrorMessage(sql_conn));
            exit_nicely();
        }
        sql_conns[i] = sql_conn;

        if (!options->append) {
            sprintf( sql, "DROP TABLE %s;", tables[i].name);
            res = PQexec(sql_conn, sql);
            PQclear(res); /* Will be an error if table does not exist */
        }

        res = PQexec(sql_conn, "BEGIN");
        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            fprintf(stderr, "BEGIN %s failed: %s\n", tables[i].name, PQerrorMessage(sql_conn));
            PQclear(res);
            exit_nicely();
        }
        PQclear(res);

        enum OsmType type = (i == t_point)?OSMTYPE_NODE:OSMTYPE_WAY;
        int numTags = exportListCount[type];
        struct taginfo *exportTags = exportList[type];
        if (!options->append) {
            sprintf(sql, "CREATE TABLE %s ( osm_id int4", tables[i].name );
            for (j=0; j < numTags; j++) {
                sprintf(tmp, ",\"%s\" %s", exportTags[j].name, exportTags[j].type);
                strcat(sql, tmp);
            }
            strcat(sql, " );\n");
            sprintf( sql + strlen(sql), "SELECT AddGeometryColumn('%s', 'way', %d, '%s', 2 );\n",
                        tables[i].name, SRID, tables[i].type );

            sprintf( sql + strlen(sql), "ALTER TABLE %s ALTER COLUMN way SET NOT NULL;\n", tables[i].name);

            res = PQexec(sql_conn, sql);
            if (PQresultStatus(res) != PGRES_COMMAND_OK) {
                fprintf(stderr, "%s failed: %s\n", sql, PQerrorMessage(sql_conn));
                PQclear(res);
                exit_nicely();
            }
            PQclear(res);
        }

        sql[0] = '\0';
        sprintf(sql, "COPY %s FROM STDIN", tables[i].name);

        res = PQexec(sql_conn, sql);
        if (PQresultStatus(res) != PGRES_COPY_IN) {
            fprintf(stderr, "%s failed: %s\n", sql, PQerrorMessage(sql_conn));
            PQclear(res);
            exit_nicely();
        }
        PQclear(res);
    }
    
    options->mid->start(options);

    return 0;
}

static void pgsql_out_stop()
{
    char sql[1024], tmp[128];
    PGresult   *res;
    int i;
    
    Options->mid->iterate_ways( pgsql_out_way );

    for (i=0; i<num_tables; i++) {
        PGconn *sql_conn = sql_conns[i];

        sprintf(tmp, "%d", i);

        /* Terminate any pending COPY */
        int stop = PQputCopyEnd(sql_conn, NULL);
        if (stop != 1) {
            fprintf(stderr, "COPY_END for %s failed: %s\n", tables[i].name, PQerrorMessage(sql_conn));
            exit_nicely();
        }

        res = PQgetResult(sql_conn);
        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            fprintf(stderr, "COPY_END for %s failed: %s\n", tables[i].name, PQerrorMessage(sql_conn));
            PQclear(res);
            exit_nicely();
        }
        PQclear(res);

        // Commit transaction
        res = PQexec(sql_conn, "COMMIT");
        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            fprintf(stderr, "COMMIT %s failed: %s\n", tables[i].name, PQerrorMessage(sql_conn));
            PQclear(res);
            exit_nicely();
        }
        PQclear(res);

        sql[0] = '\0';
        strcat(sql, "ANALYZE ");
        strcat(sql, tables[i].name);
        strcat(sql, ";\n");

        strcat(sql, "CREATE TABLE tmp AS SELECT * FROM ");
        strcat(sql, tables[i].name);
        strcat(sql, " ORDER BY way;\n");

        strcat(sql, "DROP TABLE ");
        strcat(sql, tables[i].name);
        strcat(sql, ";\n");

        strcat(sql, "ALTER TABLE tmp RENAME TO ");
        strcat(sql, tables[i].name);
        strcat(sql, ";\n");

        strcat(sql, "CREATE INDEX ");
        strcat(sql, tables[i].name);
        strcat(sql, "_index ON ");
        strcat(sql, tables[i].name);
        strcat(sql, " USING GIST (way GIST_GEOMETRY_OPS);\n");

        strcat(sql, "GRANT SELECT ON ");
        strcat(sql, tables[i].name);
        strcat(sql, " TO PUBLIC;\n");

        strcat(sql, "ANALYZE ");
        strcat(sql, tables[i].name);
        strcat(sql, ";\n");

        res = PQexec(sql_conn, sql);
        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            fprintf(stderr, "%s failed: %s\n", sql, PQerrorMessage(sql_conn));
            PQclear(res);
            exit_nicely();
        }
        PQclear(res);
        free( (void*) tables[i].name);
    }
#if 0    
    for( i=0; i<exportListCount[OSMTYPE_NODE]; i++ )
    {
      if( exportList[OSMTYPE_NODE][i].count == 0 )
        printf( "Unused: node %s\n", exportList[OSMTYPE_NODE][i].name );
    }

    for( i=0; i<exportListCount[OSMTYPE_WAY]; i++ )
    {
      if( exportList[OSMTYPE_WAY][i].count == 0 )
        printf( "Unused: way %s\n", exportList[OSMTYPE_WAY][i].name );
    }
#endif
    pgsql_out_cleanup();
    
    Options->mid->stop();
}

static int pgsql_add_node(int id, double lat, double lon, struct keyval *tags) 
{
  Options->mid->nodes_set(id, lat, lon, tags);
  pgsql_out_node(id, tags, lat, lon);
  return 0; 
}

static int pgsql_add_way(int id, int *nds, int nd_count, struct keyval *tags) 
{
  int polygon = 0;

  // Check whether the way is: (1) Exportable, (2) Maybe a polygon
  int filter = pgsql_filter_tags(OSMTYPE_WAY, tags, &polygon);

  // Memory saving hack:-
  // If we're not in slim mode and it's not wanted, we can quit right away */
  if( !Options->slim && filter ) 
    return 1;
    
  // If this isn't a polygon then it can not be part of a multipolygon
  // Hence only polygons are "pending"
  Options->mid->ways_set(id, nds, nd_count, tags, (!filter && polygon) ? 1 : 0);

  if( !polygon && !filter )
  {
    /* Get actual node data and generate output */
    struct osmNode *nodes = malloc( sizeof(struct osmNode) * nd_count );
    int count = Options->mid->nodes_get_list( nodes, nds, nd_count );
    pgsql_out_way(id, tags, nodes, count);
    free(nodes);
  }
  return 0; 
}

static int pgsql_add_relation(int id, struct member *members, int member_count, struct keyval *tags) 
{
  const char *type = getItem(tags, "type");

  // Currently we are only interested in processing multipolygons
  if (!type || strcmp(type, "multipolygon"))
      return 0;

  /* At this moment, why bother remembering relations at all?*/
  if(0)
    Options->mid->relations_set(id, members, member_count, tags);
  // (int id, struct keyval *rel_tags, struct osmNode **xnodes, struct keyval **xtags, int *xcount)
  int i, count;
  int *xcount = malloc( (member_count+1) * sizeof(int) );
  struct keyval *xtags  = malloc( (member_count+1) * sizeof(struct keyval) );
  struct osmNode **xnodes = malloc( (member_count+1) * sizeof(struct osmNode*) );
  
  count = 0;
  for( i=0; i<member_count; i++ )
  {
    /* Need to handle more than just ways... */
    if( members[i].type != OSMTYPE_WAY )
      continue;

    initList(&(xtags[count]));
    if( Options->mid->ways_get( members[i].id, &(xtags[count]), &(xnodes[count]), &(xcount[count]) ) )
      continue;
    count++;
  }
  xnodes[count] = NULL;
  xcount[count] = 0;
  
  // At some point we might want to consider storing the retreived data in the members, rather than as seperate arrays
  pgsql_out_relation(id, members, member_count, tags, xnodes, xtags, xcount);
  
  for( i=0; i<count; i++ )
  {
    resetList( &(xtags[i]) );
    free( xnodes[i] );
  }
    
  free(xcount);
  free(xtags);
  free(xnodes);
  return 0; 
}
struct output_t out_pgsql = {
        start:     pgsql_out_start,
        stop:      pgsql_out_stop,
        cleanup:   pgsql_out_cleanup,
        node_add:      pgsql_add_node,
        way_add:       pgsql_add_way,
        relation_add:  pgsql_add_relation
};
