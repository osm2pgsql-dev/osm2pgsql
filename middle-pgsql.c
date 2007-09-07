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

/* Postgres database parameters */
static char conninfo[256];

enum table_id {
    t_node, t_segment, t_node_tag, t_way_tag, t_way_seg
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
};

static struct table_desc tables [] = {
    { 
        //table: t_node,
         name: "nodes",
        start: "BEGIN;\n",
       create: "CREATE  TABLE nodes (\"id\" int4 PRIMARY KEY, \"lat\" double precision, \"lon\" double precision);\n",
      prepare: "PREPARE insert_node (int4, double precision, double precision) AS INSERT INTO nodes VALUES ($1,$2,$3);\n"
               "PREPARE get_node (int4) AS SELECT \"lat\",\"lon\" FROM nodes WHERE \"id\" = $1 LIMIT 1;\n",
         copy: "COPY nodes FROM STDIN;\n",
      analyze: "ANALYZE nodes;\n",
         stop: "COMMIT;\n"
    },
    {
        //table: t_segment,
         name: "segments",
        start: "BEGIN;\n",
       create: "CREATE  TABLE segments (\"id\" int4 PRIMARY KEY,\"from\" int4,\"to\" int4);\n",
      prepare: "PREPARE insert_segment (int4, int4, int4) AS INSERT INTO segments VALUES ($1,$2,$3);\n"
               "PREPARE get_segment (int4) AS SELECT \"from\",\"to\" FROM segments WHERE \"id\" = $1 LIMIT 1;\n",
         copy: "COPY segments FROM STDIN;\n",
      analyze: "ANALYZE segments;\n",
         stop: "COMMIT;\n"
    },
    { 
        //table: t_node_tag,
         name: "node_tag",
        start: "BEGIN;\n",
       create: "CREATE  TABLE node_tag (\"id\" int4 NOT NULL, \"key\" text, \"value\" text);\n"
               "CREATE INDEX node_tag_idx ON node_tag (\"id\");\n",
      prepare: "PREPARE insert_node_tag (int4, text, text) AS INSERT INTO node_tag VALUES ($1,$2,$3);\n"
               "PREPARE get_node_tag_key (int4, text) AS SELECT \"value\" FROM node_tag WHERE \"id\" = $1 AND \"key\" = $2 LIMIT 1;\n"
               "PREPARE get_node_tag (int4) AS SELECT \"key\",\"value\" FROM node_tag WHERE \"id\" = $1;\n",
         copy: "COPY node_tag FROM STDIN;\n",
      analyze: "ANALYZE node_tag;\n",
         stop: "COMMIT;\n"
    },
    { 
        //table: t_way_tag,
         name: "way_tag",
        start: "BEGIN;\n",
       create: "CREATE  TABLE way_tag (\"id\" int4 NOT NULL, \"key\" text, \"value\" text);\n"
               "CREATE INDEX way_tag_idx ON way_tag (\"id\");\n",
      prepare: "PREPARE insert_way_tag (int4, text, text) AS INSERT INTO way_tag VALUES ($1,$2,$3);\n"
               "PREPARE get_way_tag_key (int4, text) AS SELECT \"value\" FROM way_tag WHERE \"id\" = $1 AND \"key\" = $2 LIMIT 1;\n"
               "PREPARE get_way_tag (int4) AS SELECT \"key\",\"value\" FROM way_tag WHERE \"id\" = $1;\n",
         copy: "COPY way_tag FROM STDIN;\n",
      analyze: "ANALYZE way_tag;\n",
         stop:  "COMMIT;\n"
    },
    { 
        //table: t_way_seg,
         name: "way_seg",
        start: "BEGIN;\n",
       create: "CREATE  TABLE way_seg (\"id\" int4 NOT NULL, \"seg_id\" int4);\n"
               "CREATE INDEX way_seg_idx ON way_seg (\"id\");\n",
      prepare: "PREPARE insert_way_seg (int4, int4) AS INSERT INTO way_seg VALUES ($1,$2);\n"
               "PREPARE get_way_seg (int4) AS SELECT \"seg_id\" FROM way_seg WHERE \"id\" = $1;\n",
         copy: "COPY way_seg FROM STDIN;\n",
      analyze: "ANALYZE way_seg;\n",
         stop: "COMMIT;\n"
    }
};

static int num_tables = sizeof(tables)/sizeof(*tables);
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

void escape(char *out, int len, const char *in)
{ 
    /* Apply escaping of TEXT COPY data
    Escape: backslash itself, newline, carriage return, and the current delimiter character (tab)
    file:///usr/share/doc/postgresql-8.1.8/html/sql-copy.html
    */
    int count = 0; 
    const char *old_in = in, *old_out = out;

    if (!len)
        return;

    while(*in && count < len-3) { 
        switch(*in) {
            case '\\': *out++ = '\\'; *out++ = '\\'; count+= 2; break;
      //    case    8: *out++ = '\\'; *out++ = '\b'; count+= 2; break;
      //    case   12: *out++ = '\\'; *out++ = '\f'; count+= 2; break;
            case '\n': *out++ = '\\'; *out++ = '\n'; count+= 2; break;
            case '\r': *out++ = '\\'; *out++ = '\r'; count+= 2; break;
            case '\t': *out++ = '\\'; *out++ = '\t'; count+= 2; break;
      //    case   11: *out++ = '\\'; *out++ = '\v'; count+= 2; break;
            default:   *out++ = *in; count++; break;
        }
        in++;
    }
    *out = '\0';

    if (*in)
        fprintf(stderr, "%s truncated at %d chars: %s\n%s\n", __FUNCTION__, count, old_in, old_out);
}

static void psql_add_tag_generic(PGconn *sql_conn, int id, const char *key, const char *value)
{
    char sql[256];
    int r;
    /* TODO: 
    * Also watch for buffer overflow on long tag / value!
    */
    //sprintf(sql, "%d\t%s\t%s\n", node_id, key, value);
    sprintf(sql, "%d\t", id);
    r = PQputCopyData(sql_conn, sql, strlen(sql));
    if (r != 1) {
        fprintf(stderr, "%s - bad result %d, line %s\n", __FUNCTION__, r, sql);
        exit_nicely();
    }
    escape(sql, sizeof(sql), key);
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

    escape(sql, sizeof(sql), value);
    r = PQputCopyData(sql_conn, sql, strlen(sql));
    if (r != 1) {
        fprintf(stderr, "%s - bad result %d, line %s\n", __FUNCTION__, r, sql);
        exit_nicely();
    }

    r = PQputCopyData(sql_conn, "\n", 1);
    if (r != 1) {
        fprintf(stderr, "%s - bad result %d, newline\n", __FUNCTION__, r);
        exit_nicely();
    }
}

#if 0
static void psql_add_node_tag(int id, const char *key, const char *value)
{
    PGconn *sql_conn = sql_conns[t_node_tag];
    psql_add_tag_generic(sql_conn, id, key, value);
}
#endif
static void psql_add_way_tag(int id, const char *key, const char *value)
{
    PGconn *sql_conn = sql_conns[t_way_tag];
    psql_add_tag_generic(sql_conn, id, key, value);
}

static int pgsql_nodes_set_pos(int id, double lat, double lon)
{
    PGconn *sql_conn = sql_conns[t_node];
    char sql[128];
    int r;

    sprintf(sql, "%d\t%.15g\t%.15g\n", id, lat, lon);
    r = PQputCopyData(sql_conn, sql, strlen(sql));
    if (r != 1) {
        fprintf(stderr, "%s - bad result %d, line %s\n", __FUNCTION__, r, sql);
        exit_nicely();
    }

    return 0;
}

static int pgsql_nodes_set(int id, double lat, double lon, struct keyval *tags)
{
//    struct keyval *p;
    int r = pgsql_nodes_set_pos(id, lat,  lon);

    if (r) return r;

#if 1
    /* FIXME: This is a performance hack which interferes with a clean middle / output separation */
    out_pgsql.node(id, tags, lat, lon);
    resetList(tags);
#else
    while((p = popItem(tags)) != NULL) {
        psql_add_node_tag(id, p->key, p->value);
        freeItem(p);
    }
#endif
    return 0;
}


static int pgsql_nodes_get(struct osmNode *out, int id)
{
    PGresult   *res;
    char tmp[16];
    char const *paramValues[1];
    PGconn *sql_conn = sql_conns[t_node];

    snprintf(tmp, sizeof(tmp), "%d", id);
    paramValues[0] = tmp;
 
    res = PQexecPrepared(sql_conn, "get_node", 1, paramValues, NULL, NULL, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        fprintf(stderr, "get_node failed: %s(%d)\n", PQerrorMessage(sql_conn), PQresultStatus(res));
        PQclear(res);
        exit_nicely();
    }

    if (PQntuples(res) != 1) {
        PQclear(res);
        return 1;
    } 

    out->lat = strtod(PQgetvalue(res, 0, 0), NULL);
    out->lon = strtod(PQgetvalue(res, 0, 1), NULL);
    PQclear(res);
    return 0;
}

static int pgsql_segments_set(int id, int from, int to, struct keyval *tags)
{
    PGconn *sql_conn = sql_conns[t_segment];
    char sql[128];
    int r;

    sprintf(sql, "%d\t%d\t%d\n", id, from, to);
    r = PQputCopyData(sql_conn, sql, strlen(sql));
    if (r != 1) {
        fprintf(stderr, "%s - bad result %d, line %s\n", __FUNCTION__, r, sql);
        exit_nicely();
    }
    resetList(tags);

    return 0;
}

static int pgsql_segments_get(struct osmSegment *out, int id)
{
    PGresult   *res;
    char tmp[16];
    char const *paramValues[1];
    PGconn *sql_conn = sql_conns[t_segment];

    snprintf(tmp, sizeof(tmp), "%d", id);
    paramValues[0] = tmp;

    res = PQexecPrepared(sql_conn, "get_segment", 1, paramValues, NULL, NULL, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        fprintf(stderr, "get_segment failed: %s(%d)\n", PQerrorMessage(sql_conn), PQresultStatus(res));
        PQclear(res);
        exit_nicely();
    }

    if (PQntuples(res) != 1) {
        PQclear(res);
        return 1;
    }

    out->from = strtol(PQgetvalue(res, 0, 0), NULL, 10);
    out->to   = strtol(PQgetvalue(res, 0, 1), NULL, 10);
    PQclear(res);
    return 0;
}


static void psql_add_way_seg(int way_id, int seg_id)
{
    PGconn *sql_conn = sql_conns[t_way_seg];
    char sql[128];
    int r;

    sprintf(sql, "%d\t%d\n", way_id, seg_id);
    r = PQputCopyData(sql_conn, sql, strlen(sql));
    if (r != 1) {
        fprintf(stderr, "%s - bad result %d, line %s\n", __FUNCTION__, r, sql);
        exit_nicely();
    }
}


static int pgsql_ways_set(int way_id, struct keyval *segs, struct keyval *tags)
{
#if 1
    struct keyval *p;

    while((p = popItem(segs)) != NULL) {
        int seg_id = strtol(p->value, NULL, 10);
        psql_add_way_seg(way_id, seg_id);
        freeItem(p);
    }

    while((p = popItem(tags)) != NULL) {
        psql_add_way_tag(way_id, p->key, p->value);
        freeItem(p);
    }
#else
    // FIXME: Performance hack - output ways directly. Doesn't work in COPY model
   out_pgsql.way(&mid_pgsql, way_id, tags, segs);
   resetList(tags);
   resetList(segs);
#endif
    return 0;
}

static int *pgsql_ways_get(int id)
{
    fprintf(stderr, "TODO: %s %d\n", __FUNCTION__, id);
    return NULL;
}


static void pgsql_iterate_nodes(int (*callback)(int id, struct keyval *tags, double node_lat, double node_lon))
{
    PGresult   *res_nodes, *res_tags;
    PGconn *sql_conn_nodes = sql_conns[t_node];
    PGconn *sql_conn_tags = sql_conns[t_node_tag];
    int i, j;
    struct keyval tags;

    initList(&tags);

    res_nodes = PQexec(sql_conn_nodes, "SELECT * FROM nodes");
    if (PQresultStatus(res_nodes) != PGRES_TUPLES_OK) {
        fprintf(stderr, "%s failed: %s\n", __FUNCTION__, PQerrorMessage(sql_conn_nodes));
        PQclear(res_nodes);
        exit_nicely();
    }

    for (i = 0; i < PQntuples(res_nodes); i++) {
        const char *paramValues[1];
        const char *xid = PQgetvalue(res_nodes, i, 0);
        int          id = strtol(xid, NULL, 10);
        double node_lat = strtod(PQgetvalue(res_nodes, i, 1), NULL);
        double node_lon = strtod(PQgetvalue(res_nodes, i, 2), NULL);

        if (i %10000 == 0)
            fprintf(stderr, "\rprocessing node (%dk)", i/1000);

        paramValues[0] = xid;

        res_tags = PQexecPrepared(sql_conn_tags, "get_node_tag", 1, paramValues, NULL, NULL, 0);
        if (PQresultStatus(res_tags) != PGRES_TUPLES_OK) {
            fprintf(stderr, "get_node_tag failed: %s(%d)\n", PQerrorMessage(sql_conn_tags), PQresultStatus(res_tags));
            PQclear(res_tags);
            exit_nicely();
        }

        for (j=0; j<PQntuples(res_tags); j++) {
            const char *key   = PQgetvalue(res_tags, j, 0);
            const char *value = PQgetvalue(res_tags, j, 1);
            addItem(&tags, key, value, 0);
        }

        callback(id, &tags, node_lat, node_lon);

        PQclear(res_tags);
        resetList(&tags);
    }

    PQclear(res_nodes);
    fprintf(stderr, "\n");
}

#if 0
static struct osmSegLL *getSegLL(struct keyval *segs, int *segCount)
{
    struct keyval *p;
    struct osmSegment segment;
    struct osmNode node;
    int id;
    int count = 0;
    struct osmSegLL *segll = malloc(countList(segs) * sizeof(struct osmSegLL));

    if (!segll) {
        resetList(segs);
        *segCount = 0;
        return NULL;
    }

    while ((p = popItem(segs)) != NULL)
    {
        id = strtol(p->value, NULL, 10);
        freeItem(p);

        if (pgsql_segments_get(&segment, id))
            continue;

        if (pgsql_nodes_get(&node, segment.from))
            continue;

        segll[count].lon0 = node.lon;
        segll[count].lat0 = node.lat;

        if (pgsql_nodes_get(&node, segment.to))
            continue;

        segll[count].lon1 = node.lon;
        segll[count].lat1 = node.lat;

        count++;
    }
    *segCount = count;
    return segll;
}
#endif

void getTags(int id, struct keyval *tags)
{
    PGconn *sql_conn_tags = sql_conns[t_way_tag];
    PGresult   *res_tags;
    char tmp[16];
    const char *paramValues[1];
    int j;

    snprintf(tmp, sizeof(tmp), "%d", id);
    paramValues[0] = tmp;

    res_tags = PQexecPrepared(sql_conn_tags, "get_way_tag", 1, paramValues, NULL, NULL, 0);
    if (PQresultStatus(res_tags) != PGRES_TUPLES_OK) {
        fprintf(stderr, "get_way_tag failed: %s(%d)\n", PQerrorMessage(sql_conn_tags), PQresultStatus(res_tags));
        PQclear(res_tags);
        exit_nicely();
    }

    for (j=0; j<PQntuples(res_tags); j++) {
        const char *key   = PQgetvalue(res_tags, j, 0);
        const char *value = PQgetvalue(res_tags, j, 1);
        addItem(tags, key, value, 0);
    }
    PQclear(res_tags);
}


static void pgsql_iterate_ways(int (*callback)(int id, struct keyval *tags, struct osmSegLL *segll, int count))
{
    char sql[2048];
    PGresult   *res_ways, *res_nodes;
    int i, j, count = 0;
    struct keyval tags;
    struct osmSegLL *segll;
    PGconn *sql_conn_segs = sql_conns[t_way_seg];

    initList(&tags);

    fprintf(stderr, "\nRetrieving way list\n");

/*
gis=> select w.id,s.id,nf.lat,nf.lon,nt.lat,nt.lon from way_seg as w,nodes as nf, nodes as nt, segments as s where w.seg_id=s.id and nf.id=s.from and nt.id=s.to limit 10;
   id    |    id    |    lat    |    lon    |    lat    |    lon
---------+----------+-----------+-----------+-----------+-----------
 3186577 | 10872312 | 51.720753 | -0.362055 | 51.720567 | -0.364115
 3186577 | 10872311 | 51.720979 | -0.360532 | 51.720753 | -0.362055
 3186577 | 10872310 | 51.721258 | -0.359137 | 51.720979 | -0.360532
*/

    res_ways = PQexec(sql_conn_segs, "SELECT id from way_seg GROUP BY id");
    if (PQresultStatus(res_ways) != PGRES_TUPLES_OK) {
        fprintf(stderr, "%s(%d) failed: %s\n", __FUNCTION__, __LINE__, PQerrorMessage(sql_conn_segs));
        PQclear(res_ways);
        exit_nicely();
    }

    //fprintf(stderr, "\nIterating ways\n");
    for (i = 0; i < PQntuples(res_ways); i++) {
        int id = strtol(PQgetvalue(res_ways, i, 0), NULL, 10);
        int segCount;

        if (count++ %1000 == 0)
                fprintf(stderr, "\rprocessing way (%dk)", count/1000);

        getTags(id, &tags);

        sprintf(sql,"SELECT nf.lat,nf.lon,nt.lat,nt.lon "
                    "FROM way_seg AS w,nodes AS nf, nodes AS nt, segments AS s "
                    "WHERE w.seg_id=s.id AND nf.id=s.from AND nt.id=s.to AND w.id=%d", id);

        res_nodes = PQexec(sql_conn_segs, sql);
        if (PQresultStatus(res_nodes) != PGRES_TUPLES_OK) {
            fprintf(stderr, "%s(%d) failed: %s\n", __FUNCTION__, __LINE__, PQerrorMessage(sql_conn_segs));
            PQclear(res_nodes);
            exit_nicely();
        }

        segCount = PQntuples(res_nodes);
        segll = malloc(segCount * sizeof(struct osmSegLL));

        if (segll) {
            for (j=0; j<segCount; j++) {
                segll[j].lat0 = strtod(PQgetvalue(res_nodes, j, 0), NULL);
                segll[j].lon0 = strtod(PQgetvalue(res_nodes, j, 1), NULL);
                segll[j].lat1 = strtod(PQgetvalue(res_nodes, j, 2), NULL);
                segll[j].lon1 = strtod(PQgetvalue(res_nodes, j, 3), NULL);
            }
            callback(id, &tags, segll, segCount);
            free(segll);
        }
        PQclear(res_nodes);
        resetList(&tags);
    }

    PQclear(res_ways);
    fprintf(stderr, "\n");
}


static void pgsql_analyze(void)
{
    PGresult   *res;
    int i;

    for (i=0; i<num_tables; i++) {
        PGconn *sql_conn = sql_conns[i];
 
        if (tables[i].analyze) {
            res = PQexec(sql_conn, tables[i].analyze);
            if (PQresultStatus(res) != PGRES_COMMAND_OK) {
                fprintf(stderr, "%s failed: %s\n", tables[i].analyze, PQerrorMessage(sql_conn));
                PQclear(res);
                exit_nicely();
            }
            PQclear(res);
        }
    }
}

static void pgsql_end(void)
{
    PGresult   *res;
    int i;

    for (i=0; i<num_tables; i++) {
        PGconn *sql_conn = sql_conns[i];
 
        /* Terminate any pending COPY */
         if (tables[i].copy) {
            int stop = PQputCopyEnd(sql_conn, NULL);
            if (stop != 1) {
                fprintf(stderr, "COPY_END for %s failed: %s\n", tables[i].copy, PQerrorMessage(sql_conn));
                exit_nicely();
            }

            res = PQgetResult(sql_conn);
            if (PQresultStatus(res) != PGRES_COMMAND_OK) {
                fprintf(stderr, "COPY_END for %s failed: %s\n", tables[i].copy, PQerrorMessage(sql_conn));
                PQclear(res);
                exit_nicely();
            }
            PQclear(res);
        }

        // Commit transaction
        if (tables[i].stop) {
            res = PQexec(sql_conn, tables[i].stop);
            if (PQresultStatus(res) != PGRES_COMMAND_OK) {
                fprintf(stderr, "%s failed: %s\n", tables[i].stop, PQerrorMessage(sql_conn));
                PQclear(res);
                exit_nicely();
            }
            PQclear(res);
        }

    }
}

#define __unused  __attribute__ ((unused))
static int pgsql_start(const char *db, int latlong __unused)
{
    char sql[2048];
    PGresult   *res;
    int i;
    int dropcreate = 1;

    snprintf(conninfo, sizeof(conninfo), "dbname = %s", db);

    /* We use a connection per table to enable the use of COPY */
    sql_conns = calloc(num_tables, sizeof(PGconn *));
    assert(sql_conns);

    for (i=0; i<num_tables; i++) {
        PGconn *sql_conn;

        fprintf(stderr, "Setting up table: %s\n", tables[i].name);
        sql_conn = PQconnectdb(conninfo);

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
            res = PQexec(sql_conn, tables[i].start);
            if (PQresultStatus(res) != PGRES_COMMAND_OK) {
                fprintf(stderr, "%s failed: %s\n", tables[i].start, PQerrorMessage(sql_conn));
                PQclear(res);
                exit_nicely();
            }
            PQclear(res);
        }

        if (dropcreate && tables[i].create) {
            res = PQexec(sql_conn, tables[i].create);
            if (PQresultStatus(res) != PGRES_COMMAND_OK) {
                fprintf(stderr, "%s failed: %s\n", tables[i].create, PQerrorMessage(sql_conn));
                PQclear(res);
                exit_nicely();
            }
            PQclear(res);
        }

        if (tables[i].prepare) {
            res = PQexec(sql_conn, tables[i].prepare);
            if (PQresultStatus(res) != PGRES_COMMAND_OK) {
                fprintf(stderr, "%s failed: %s\n", tables[i].prepare, PQerrorMessage(sql_conn));
                PQclear(res);
                exit_nicely();
            }
            PQclear(res);
        }

        if (tables[i].copy) {
            res = PQexec(sql_conn, tables[i].copy);
            if (PQresultStatus(res) != PGRES_COPY_IN) {
                fprintf(stderr, "%s failed: %s\n", tables[i].copy, PQerrorMessage(sql_conn));
                PQclear(res);
                exit_nicely();
            }
            PQclear(res);
        }
    }

    return 0;
}

static void pgsql_stop(void)
{
    //PGresult   *res;
    PGconn *sql_conn;
    int i;

   for (i=0; i<num_tables; i++) {
        //fprintf(stderr, "Stopping table: %s\n", tables[i].name);
        sql_conn = sql_conns[i];
#if 0
        if (tables[i].stop) {
            res = PQexec(sql_conn, tables[i].stop);
            if (PQresultStatus(res) != PGRES_COMMAND_OK) {
                fprintf(stderr, "%s failed: %s\n", tables[i].stop, PQerrorMessage(sql_conn));
                PQclear(res);
                exit_nicely();
            }
            PQclear(res);
        }
#endif
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
        segments_set:   pgsql_segments_set,
        segments_get:   pgsql_segments_get,
        nodes_set:      pgsql_nodes_set,
        nodes_get:      pgsql_nodes_get,
        ways_set:       pgsql_ways_set,
        ways_get:       pgsql_ways_get,
        iterate_nodes:  pgsql_iterate_nodes,
        iterate_ways:   pgsql_iterate_ways
};
