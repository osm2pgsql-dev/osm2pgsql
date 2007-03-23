/* Implements the mid-layer processing for osm2pgsql
 * using several PostgreSQL tables
 * 
 * This layer stores data read in from the planet.osm file
 * and is then read by the backend processing code to
 * emit the final geometry-enabled output formats
*/
 
#define _GNU_SOURCE

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <libpq-fe.h>

#include "osmtypes.h"
#include "output.h"
#include "output-pgsql.h"
#include "build_geometry.h"
#include "middle-pgsql.h"

/* Postgres database parameters */
static const char *conninfo = "dbname = gis";

enum table_id {
    t_point, t_line, t_poly, t_roads
};

/* Tables to output */
static struct {
    //enum table_id table;
    const char *name;
    const char *type;
} tables [] = {
    { name: "planet_osm_point",   type: "POINT"     },
    { name: "planet_osm_line",    type: "LINESTRING"},
    { name: "planet_osm_polygon", type: "GEOMETRY"  },
    { name: "planet_osm_roads",   type: "LINESTRING"}
};
static const unsigned int num_tables = sizeof(tables)/sizeof(*tables);

/* Table columns, representing key= tags */
static struct {
    const char *name;
    const char *type;
    const int polygon;
} exportTags[] = {
    {"name",    "text", 0},
    {"place",   "text", 0},
    {"landuse", "text", 1},
    {"leisure", "text", 1},
    {"natural", "text", 1},
    {"man_made","text", 0},
    {"waterway","text", 0},
    {"highway", "text", 0},
    {"foot",    "text", 0},
    {"horse",   "text", 0},
    {"bicycle", "text", 0},
    {"motorcar","text", 0},
    {"residence","text", 0},
    {"railway", "text", 0},
    {"amenity", "text", 1},
    {"tourism", "text", 1},
    {"learning","text", 0},
    {"building","text", 1},
    {"bridge",  "text", 0},
    {"layer",   "text", 0},
    {"junction","text", 0},
    {"sport",   "text", 1},
    {"route",   "text", 0},
    {"aeroway", "text", 0},
    {"z_order", "int4", 0}
};
static const unsigned int numTags = sizeof(exportTags) / sizeof(*exportTags);

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



static int add_z_order_polygon(struct keyval *tags, int *roads)
{
    /* Discard polygons with the tag natural=coastline */
    const char *natural = getItem(tags, "natural");
    *roads = 0;

    if (natural && !strcmp(natural, "coastline"))
        return 1;

    return 0;
}


static int add_z_order_line(struct keyval *tags, int *roads)
{
    const char *layer   = getItem(tags, "layer");
    const char *highway = getItem(tags, "highway");
    const char *bridge  = getItem(tags, "bridge");
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
                *roads    = layers[i].roads;
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

    snprintf(z, sizeof(z), "%d", z_order);
    addItem(tags, "z_order", z, 0);

    return 0;
}

static int add_z_order(struct keyval* tags, int polygon, int *roads)
{
    return polygon ? add_z_order_polygon(tags, roads) : add_z_order_line(tags, roads);
}


static void pgsql_out_cleanup(void)
{
    unsigned int i;

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
//    char *tmp;
//    size_t len;
    unsigned int i, r, export = 0;
    PGconn *sql_conn = sql_conns[t_point];

    for (i=0; i < numTags; i++) {
        if ((v = getItem(tags, exportTags[i].name)))
            export = 1; 
    }

    if (!export)
       return 0;

    sprintf(sql, "%d\t", id);
    r = PQputCopyData(sql_conn, sql, strlen(sql));
    if (r != 1) {
        fprintf(stderr, "%s - bad result %d, line %s\n", __FUNCTION__, r, sql);
        exit_nicely();
    }

    for (i=0; i < numTags; i++) {
        if ((v = getItem(tags, exportTags[i].name)))
            escape(sql, sizeof(sql), v);
        else
            sprintf(sql, "\\N");

        r = PQputCopyData(sql_conn, sql, strlen(sql));
        if (r != 1) {
            fprintf(stderr, "%s - bad result %d, line %s\n", __FUNCTION__, r, sql);
            exit_nicely();
        }
        r = PQputCopyData(sql_conn, "\t", 1);
        if (r != 1) {
            fprintf(stderr, "%s - bad result %d, tab\n", __FUNCTION__, r);
            exit_nicely();
        }
    }

    sprintf(sql, "SRID=4326;POINT(%.15g %.15g)", node_lon, node_lat);
    r = PQputCopyData(sql_conn, sql, strlen(sql));
    if (r != 1) {
        fprintf(stderr, "%s - bad result %d, line %s\n", __FUNCTION__, r, sql);
        exit_nicely();
    }
#if 0
//    sprintf(sql, "POINT(%.15g %.15g)", node_lon, node_lat);
    sprintf(sql, "POINT(%.15g %.15g)", node_lon, node_lat);
    tmp = get_point_wkb(sql, &len);
    
    if (id == 17959841) fprintf(stderr, "\n%d %s ", id, sql);
    for(i=0; i<len && (i<<1)< sizeof(sql)-1; i++)
        sprintf(&sql[i<<1], "%02u", tmp[i]);
    sql[i<<1] = '\0';
    if (id == 17959841) fprintf(stderr, "%s\n",sql);

    if (i != len)
           fprintf(stderr, "truncated - %d not %zd", i, len);
    r = PQputCopyData(sql_conn, sql, i<<1);
    if (r != 1) {
        fprintf(stderr, "%s - bad result %d, line %s\n", __FUNCTION__, r, sql);
        exit_nicely();
    }
    free(tmp);
    //fprintf(stderr, "%s\n", sql);
#endif
    sprintf(sql, "\n");
    r = PQputCopyData(sql_conn, sql, strlen(sql));
    if (r != 1) {
        fprintf(stderr, "%s - bad result %d, line %s\n", __FUNCTION__, r, sql);
        exit_nicely();
    }
 
    return 0;
}


static size_t WKT(struct osmSegLL *segll, int count, int polygon)
{
    double x0, y0, x1, y1;
    int i;

    for(i=0; i<count; i++) {
        x0 = segll[i].lon0;
        y0 = segll[i].lat0;
        x1 = segll[i].lon1;
        y1 = segll[i].lat1;
        add_segment(x0,y0,x1,y1);
    }

    return  build_geometry(polygon);
}


static void write_wkts(int id, struct keyval *tags, size_t wkt_size, PGconn *sql_conn)
{
    unsigned int i, j, r;
    char sql[2048];
    const char*v;

    for (i=0;i<wkt_size;i++)
    {
        char *wkt = get_wkt(i);
        if (strlen(wkt)) {
            sprintf(sql, "%d\t", id);
            r = PQputCopyData(sql_conn, sql, strlen(sql));
            if (r != 1) {
                fprintf(stderr, "%s - bad result %d, line %s\n", __FUNCTION__, r, sql);
                exit_nicely();
            }
            for (j=0; j < sizeof(exportTags) / sizeof(exportTags[0]); j++) {
                if ((v = getItem(tags, exportTags[j].name)))
                    escape(sql, sizeof(sql), v);
                else
                    sprintf(sql, "\\N");

                r = PQputCopyData(sql_conn, sql, strlen(sql));
                if (r != 1) {
                    fprintf(stderr, "%s - bad result %d, line %s\n", __FUNCTION__, r, sql);
                    exit_nicely();
                }
                r = PQputCopyData(sql_conn, "\t", 1);
                if (r != 1) {
                    fprintf(stderr, "%s - bad result %d, tab\n", __FUNCTION__, r);
                    exit_nicely();
                }
            }

            sprintf(sql, "SRID=4326;");
            r = PQputCopyData(sql_conn, sql, strlen(sql));
            if (r != 1) {
                fprintf(stderr, "%s - bad result %d, line %s\n", __FUNCTION__, r, sql);
                exit_nicely();
            }
            r = PQputCopyData(sql_conn, wkt, strlen(wkt));
            if (r != 1) {
                fprintf(stderr, "%s - bad result %d, line %s\n", __FUNCTION__, r, wkt);
                exit_nicely();
            }
            sprintf(sql, "\n");
            r = PQputCopyData(sql_conn, sql, strlen(sql));
            if (r != 1) {
                fprintf(stderr, "%s - bad result %d, line %s\n", __FUNCTION__, r, sql);
                exit_nicely();
            }
        }
        free(wkt);
    }
}

/*
COPY planet_osm (osm_id, name, place, landuse, leisure, "natural", man_made, waterway, highway, railway, amenity, tourism, learning, bu
ilding, bridge, layer, way) FROM stdin;
198497  Bedford Road    \N      \N      \N      \N      \N      \N      residential     \N      \N      \N      \N      \N      \N    \N       0102000020E610000004000000452BF702B342D5BF1C60E63BF8DF49406B9C4D470037D5BF5471E316F3DF4940DFA815A6EF35D5BF9AE95E27F5DF4940B41EB
E4C1421D5BF24D06053E7DF4940
212696  Oswald Road     \N      \N      \N      \N      \N      \N      minor   \N      \N      \N      \N      \N      \N      \N    0102000020E610000004000000467D923B6C22D5BFA359D93EE4DF4940B3976DA7AD11D5BF84BBB376DBDF4940997FF44D9A06D5BF4223D8B8FEDF49404D158C4AEA04D
5BF5BB39597FCDF4940
*/
static int pgsql_out_way(int id, struct keyval *tags, struct osmSegLL *segll, int count)
{
    const char *v;
    unsigned int i;
    size_t wkt_size;
    int polygon = 0, export = 0;
    int roads = 0;

    for (i=0; i < sizeof(exportTags) / sizeof(exportTags[0]); i++) {
        if ((v = getItem(tags, exportTags[i].name))) {
            polygon |= exportTags[i].polygon;
            export = 1;
        }
    }

    if (!export)
         return 0;

    if (add_z_order(tags, polygon, &roads))
         return 0;

    wkt_size = WKT(segll, count, polygon); 

    write_wkts(id, tags, wkt_size, sql_conns[polygon?t_poly:t_line]);

    if (roads)
        write_wkts(id, tags, wkt_size, sql_conns[t_roads]);

    clear_wkts();
    return 0;
}


static int pgsql_out_start()
{
    char sql[1024], tmp[128];
    PGresult   *res;
    unsigned int i,j;

    /* We use a connection per table to enable the use of COPY */
    sql_conns = calloc(num_tables, sizeof(PGconn *));
    assert(sql_conns);

    for (i=0; i<num_tables; i++) {
        PGconn *sql_conn;

        fprintf(stderr, "Setting up table: %s\n", tables[i].name);
        sql_conn = PQconnectdb(conninfo);

        /* Check to see that the backend connection was successfully made */
        if (PQstatus(sql_conn) != CONNECTION_OK) {
            fprintf(stderr, "Connection to database failed: %s", PQerrorMessage(sql_conn));
            exit_nicely();
        }
        sql_conns[i] = sql_conn;

        sql[0] = '\0';
        strcat(sql, "DROP TABLE ");
        strcat(sql, tables[i].name);
        res = PQexec(sql_conn, sql);
        PQclear(res); /* Will be an error if table does not exist */

        res = PQexec(sql_conn, "BEGIN");
        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            fprintf(stderr, "BEGIN %s failed: %s", tables[i].name, PQerrorMessage(sql_conn));
            PQclear(res);
            exit_nicely();
        }
        PQclear(res);

        sql[0] = '\0';
        strcat(sql, "CREATE TABLE ");
        strcat(sql, tables[i].name);
        strcat(sql, " ( osm_id int4");
        for (j=0; j < sizeof(exportTags) / sizeof(exportTags[0]); j++) {
            sprintf(tmp, ",\"%s\" %s", exportTags[j].name, exportTags[j].type);
            strcat(sql, tmp);
        }
        strcat(sql, " );\n");
        strcat(sql, "SELECT AddGeometryColumn('");
        strcat(sql, tables[i].name);
        strcat(sql, "', 'way', 4326, '");
        strcat(sql, tables[i].type);
        strcat(sql, "', 2 );\n");

        res = PQexec(sql_conn, sql);
        if (PQresultStatus(res) != PGRES_TUPLES_OK) {
            fprintf(stderr, "%s failed: %s", sql, PQerrorMessage(sql_conn));
            PQclear(res);
            exit_nicely();
        }
        PQclear(res);

        sql[0] = '\0';
        strcat(sql, "COPY ");
        strcat(sql, tables[i].name);
        strcat(sql, " FROM STDIN");

        res = PQexec(sql_conn, sql);
        if (PQresultStatus(res) != PGRES_COPY_IN) {
            fprintf(stderr, "%s failed: %s", sql, PQerrorMessage(sql_conn));
            PQclear(res);
            exit_nicely();
        }
        PQclear(res);
    }

    return 0;
}

static void pgsql_out_stop(void)
{
    char sql[1024], tmp[128];
    PGresult   *res;
    unsigned int i;

    for (i=0; i<num_tables; i++) {
        PGconn *sql_conn = sql_conns[i];
 
        /* Terminate any pending COPY */
        int stop = PQputCopyEnd(sql_conn, NULL);
        if (stop != 1) {
            fprintf(stderr, "COPY_END for %s failed: %s", tables[i].name, PQerrorMessage(sql_conn));
            exit_nicely();
        }

        res = PQgetResult(sql_conn);
        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            fprintf(stderr, "COPY_END for %s failed: %s", tables[i].name, PQerrorMessage(sql_conn));
            PQclear(res);
            exit_nicely();
        }
        PQclear(res);

        // Commit transaction
        res = PQexec(sql_conn, "COMMIT");
        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            fprintf(stderr, "COMMIT %s failed: %s", tables[i].name, PQerrorMessage(sql_conn));
            PQclear(res);
            exit_nicely();
        }
        PQclear(res);

        sql[0] = '\0';
        strcat(sql, "VACUUM ANALYZE ");
        strcat(sql, tables[i].name);
        strcat(sql, ";\n");

        strcat(sql, "CREATE INDEX way_index");
        sprintf(tmp, "%d", i);
        strcat(sql, tmp);
        strcat(sql, " ON ");
        strcat(sql, tables[i].name);
        strcat(sql, " USING GIST (way GIST_GEOMETRY_OPS);\n");

        strcat(sql, "CREATE INDEX z_index");
        strcat(sql, tmp);
        strcat(sql, " ON ");
        strcat(sql, tables[i].name);
        strcat(sql, " (z_order);\n");

        strcat(sql, "ALTER TABLE ");
        strcat(sql, tables[i].name);
        strcat(sql, " ALTER COLUMN way SET NOT NULL;\n");
#if 0
        // z_order is now exported as a normal key column on all tables
        if (i == t_line) {
            strcat(sql, "ALTER TABLE ");
            strcat(sql, tables[i].name);
            strcat(sql, " ADD COLUMN z_order int4 default 0;\n");
        }
#endif
        strcat(sql, "CLUSTER way_index");
        strcat(sql, tmp);
        strcat(sql, " ON ");
        strcat(sql, tables[i].name);
        strcat(sql, ";\n");

        strcat(sql, "VACUUM ANALYZE ");
        strcat(sql, tables[i].name);
        strcat(sql, ";\n");
        
        res = PQexec(sql_conn, sql);
        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            fprintf(stderr, "%s failed: %s", sql, PQerrorMessage(sql_conn));
            PQclear(res);
            exit_nicely();
        }
        PQclear(res);
    }

    pgsql_out_cleanup();
}

 
struct output_t out_pgsql = {
        start:     pgsql_out_start,
        stop:      pgsql_out_stop,
        cleanup:   pgsql_out_cleanup,
        node:      pgsql_out_node,
        way:       pgsql_out_way
};
