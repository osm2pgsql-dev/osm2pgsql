/*
#-----------------------------------------------------------------------------
# osm2pgsql - converts planet.osm file into PostgreSQL
# compatible output suitable to be rendered by mapnik
# Use: osm2pgsql planet.osm
#-----------------------------------------------------------------------------
# Original Python implementation by Artem Pavlenko
# Re-implementation by Jon Burgess, Copyright 2006
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
#-----------------------------------------------------------------------------
*/

#define _GNU_SOURCE

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <libxml/xmlstring.h>
#include <libxml/xmlreader.h>

#include <libpq-fe.h>

#include "build_geometry.h"

#if 0
#define DEBUG printf
#else
#define DEBUG(x, ...)
#endif

struct tagDesc {
    const char *name;
    const char *type;
    const int polygon;
}; 

static struct tagDesc exportTags[] = {
    {"name",    "text", 0},
    {"place",   "text", 0},
    {"landuse", "text", 1},
    {"leisure", "text", 1},
    {"natural", "text", 1},
    {"man_made","text", 0},
    {"waterway","text", 0},
    {"highway", "text", 0},
    {"railway", "text", 0},
    {"amenity", "text", 1},
    {"tourism", "text", 0},
    {"learning","text", 0},
    {"building","text", 1},
    {"bridge",  "text", 0},
    {"layer",   "text", 0}
};

/* Postgres database parameters */
#define TABLE_NAME "planet_osm"
static const char *conninfo = "dbname = gis";

struct osmNode {
    double lon;
    double lat;
};

struct osmSegment {
    int from;
    int to;
};

struct osmWay {
    char *values;
    char *wkt;
};

static int count_node,    count_all_node,    max_node;
static int count_segment, count_all_segment, max_segment;
static int count_way,     count_all_way,     max_way;
static int count_way_seg;


struct keyval {
    char *key;
    char *value;
    struct keyval *next;
    struct keyval *prev;
};

/* Since {node,segment,way} elements are not nested we can guarantee the 
   keys and values in an end tag must match those of the corresponding 
   start tag and can therefore be cached.
*/
static double node_lon, node_lat;
static struct keyval keys, tags, segs;

PGconn *sql_conn;

static void exit_nicely(PGconn *sql_conn)
{
    PQfinish(sql_conn);
    exit(1);
}

int segments_set(int id, int from, int to)
{
    PGresult   *res;
    char *paramValues[3];

    asprintf(&paramValues[0], "%d", id);
    asprintf(&paramValues[1], "%d", from);
    asprintf(&paramValues[2], "%d", to);

    res = PQexecPrepared(sql_conn, "insert_segment", 3, (const char * const *)paramValues, NULL, NULL, 0);

    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        fprintf(stderr, "insert_segment failed: %s", PQerrorMessage(sql_conn));
        PQclear(res);
        exit_nicely(sql_conn);
    }

    PQclear(res);
    free(paramValues[0]);
    free(paramValues[1]);
    free(paramValues[2]);
    return 0;
}

int segments_get(struct osmSegment *out, int id)
{
    PGresult   *res;
    char *paramValues[1];

    asprintf(&paramValues[0], "%d", id);

    res = PQexecPrepared(sql_conn, "get_segment", 1, (const char * const *)paramValues, NULL, NULL, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        fprintf(stderr, "get_segment failed: %s(%d)\n", PQerrorMessage(sql_conn), PQresultStatus(res));
        PQclear(res);
        exit_nicely(sql_conn);
    }

    if (PQntuples(res) != 1) {
        PQclear(res);
        free(paramValues[0]);
        return 1;
    }

    out->from = strtoul(PQgetvalue(res, 0, 0), NULL, 10);
    out->to   = strtoul(PQgetvalue(res, 0, 1), NULL, 10);
    PQclear(res);
    free(paramValues[0]);
    return 0;
}


int nodes_set(int id, double lat, double lon)
{
    PGresult   *res;
    char * paramValues[3];

    asprintf(&paramValues[0], "%d", id);
    asprintf(&paramValues[1], "%f", lat);
    asprintf(&paramValues[2], "%f", lon);

    res = PQexecPrepared(sql_conn, "insert_node", 3, (const char * const *)paramValues, NULL, NULL, 0);

    if (PQresultStatus(res) != PGRES_COMMAND_OK)
    {
        fprintf(stderr, "insert_node failed: %s", PQerrorMessage(sql_conn));
        PQclear(res);
        exit_nicely(sql_conn);
    }

    PQclear(res);
    free(paramValues[0]);
    free(paramValues[1]);
    free(paramValues[2]);
    return 0;
}

int nodes_get(struct osmNode *out, int id)
{
    PGresult   *res;
    char *paramValues[1];

    asprintf(&paramValues[0], "%d", id);

    res = PQexecPrepared(sql_conn, "get_node", 1, (const char * const *)paramValues, NULL, NULL, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        fprintf(stderr, "get_node failed: %s(%d)\n", PQerrorMessage(sql_conn), PQresultStatus(res));
        PQclear(res);
        exit_nicely(sql_conn);
    }

    if (PQntuples(res) != 1) {
        PQclear(res);
        free(paramValues[0]);
        return 1;
    } 

    out->lat = strtod(PQgetvalue(res, 0, 0), NULL);
    out->lon = strtod(PQgetvalue(res, 0, 1), NULL);
    PQclear(res);
    free(paramValues[0]);
    return 0;
}

void usage(const char *arg0)
{
    fprintf(stderr, "Usage error:\n\t%s planet.osm\n", arg0);
    fprintf(stderr, "or\n\tgzip -dc planet.osm.gz | %s -\n", arg0);
}

void initList(struct keyval *head)
{
    head->next = head;
    head->prev = head;
    head->key = NULL;
    head->value = NULL;
}

void freeItem(struct keyval *p)
{
    free(p->key);
    free(p->value);
    free(p);
}


unsigned int countList(struct keyval *head) 
{
    struct keyval *p = head->next;
    unsigned int count = 0;	

    while(p != head) {
        count++;
        p = p->next;
    }
    return count;
}

int listHasData(struct keyval *head) 
{
    return (head->next != head);
}


char *getItem(struct keyval *head, const char *name)
{
    struct keyval *p = head->next;
    while(p != head) {
        if (!strcmp(p->key, name))
            return p->value;
        p = p->next;
    }
    return NULL;
}	


struct keyval *popItem(struct keyval *head)
{
    struct keyval *p = head->next;
    if (p == head)
        return NULL;

    head->next = p->next;
    p->next->prev = head;

    p->next = NULL;
    p->prev = NULL;

    return p;
}	


void pushItem(struct keyval *head, struct keyval *item)
{
    item->next = head;
    item->prev = head->prev;
    head->prev->next = item;
    head->prev = item;
}	

int addItem(struct keyval *head, const char *name, const char *value, int noDupe)
{
    struct keyval *item;

    if (noDupe) {
        item = head->next;
        while (item != head) {
            if (!strcmp(item->value, value) && !strcmp(item->key, name))
                return 1;
            item = item->next;
        }
    }

    item = malloc(sizeof(struct keyval));

    if (!item) {
        fprintf(stderr, "Error allocating keyval\n");
        return 2;
    }

    item->key   = strdup(name);
    item->value = strdup(value);

    item->next = head->next;
    item->prev = head;
    head->next->prev = item;
    head->next = item;

    return 0;
}

void resetList(struct keyval *head) 
{
    struct keyval *item;
	
    while((item = popItem(head))) 
        freeItem(item);
}

size_t WKT(int polygon)
{
    struct keyval *p;
    struct osmSegment segment;
    struct osmNode node;
    int id;
    double x0, y0, x1, y1;

    while (listHasData(&segs))
    {
        p = popItem(&segs);
        id = strtoul(p->value, NULL, 10);
        freeItem(p);

        if (segments_get(&segment, id))
            continue;

        if (nodes_get(&node, segment.from))
            continue;

        x0 = node.lon;
        y0 = node.lat;

        if (nodes_get(&node, segment.to))
            continue;

        x1 = node.lon;
        y1 = node.lat;

        add_segment(x0,y0,x1,y1);
    }
    return  build_geometry(polygon);
}


void StartElement(xmlTextReaderPtr reader, const xmlChar *name)
{
    xmlChar *xid, *xlat, *xlon, *xfrom, *xto, *xk, *xv;
    int id, to, from;
    char *k;

    if (xmlStrEqual(name, BAD_CAST "node")) {
        xid  = xmlTextReaderGetAttribute(reader, BAD_CAST "id");
        xlon = xmlTextReaderGetAttribute(reader, BAD_CAST "lon");
        xlat = xmlTextReaderGetAttribute(reader, BAD_CAST "lat");
        assert(xid); assert(xlon); assert(xlat);
        id  = strtoul((char *)xid, NULL, 10);
        node_lon = strtod((char *)xlon, NULL);
        node_lat = strtod((char *)xlat, NULL);

        if (id > max_node) 
            max_node = id;

        count_all_node++;
        if (count_all_node%10000 == 0) 
            fprintf(stderr, "\rProcessing: Node(%dk)", count_all_node/1000);

        nodes_set(id, node_lat, node_lon);

        DEBUG("NODE(%d) %f %f\n", id, node_lon, node_lat);
        addItem(&keys, "id", (char *)xid, 0);

        xmlFree(xid);
        xmlFree(xlon);
        xmlFree(xlat);
    } else if (xmlStrEqual(name, BAD_CAST "segment")) {
        xid   = xmlTextReaderGetAttribute(reader, BAD_CAST "id");
        xfrom = xmlTextReaderGetAttribute(reader, BAD_CAST "from");
        xto   = xmlTextReaderGetAttribute(reader, BAD_CAST "to");
        assert(xid); assert(xfrom); assert(xto);
        id   = strtoul((char *)xid, NULL, 10);
        from = strtoul((char *)xfrom, NULL, 10);
        to   = strtoul((char *)xto, NULL, 10);

        if (id > max_segment) 
            max_segment = id;

        if (count_all_segment == 0) {
            PGresult   *res;
            res = PQexec(sql_conn, "ANALYZE tmp_nodes");
            if (PQresultStatus(res) != PGRES_COMMAND_OK) {
                fprintf(stderr, "ANALYZE tmp_nodes failed: %s", PQerrorMessage(sql_conn));
                PQclear(res);
                exit_nicely(sql_conn);
            }
            PQclear(res);

            fprintf(stderr, "\n");
        }

        count_all_segment++;
        if (count_all_segment%10000 == 0) 
            fprintf(stderr, "\rProcessing: Segment(%dk)", count_all_segment/1000);

        if (from != to) {
            segments_set(id, from, to);
            count_segment++;
            DEBUG("SEGMENT(%d) %d, %d\n", id, from, to);
        }

        xmlFree(xid);
        xmlFree(xfrom);
        xmlFree(xto);
    } else if (xmlStrEqual(name, BAD_CAST "tag")) {
        char *p;
        xk = xmlTextReaderGetAttribute(reader, BAD_CAST "k");
        xv = xmlTextReaderGetAttribute(reader, BAD_CAST "v");
        assert(xk); assert(xv);
        k  = (char *)xmlStrdup(xk);

        while ((p = strchr(k, ':')))
            *p = '_';
        while ((p = strchr(k, ' ')))
            *p = '_';

        addItem(&tags, k, (char *)xv, 0);
        DEBUG("\t%s = %s\n", xk, xv);
        xmlFree(k);
        xmlFree(xk);
        xmlFree(xv);
    } else if (xmlStrEqual(name, BAD_CAST "way")) {
        xid  = xmlTextReaderGetAttribute(reader, BAD_CAST "id");
        assert(xid);
        id   = strtoul((char *)xid, NULL, 10);
        addItem(&keys, "id", (char *)xid, 0);
        DEBUG("WAY(%s)\n", xid);

        if (id > max_way)
            max_way = id;

        if (count_all_way == 0) {
            fprintf(stderr, "\n");
            PGresult   *res;
            res = PQexec(sql_conn, "ANALYZE tmp_segments");
            if (PQresultStatus(res) != PGRES_COMMAND_OK) {
                fprintf(stderr, "ANALYZE tmp_segments failed: %s", PQerrorMessage(sql_conn));
                PQclear(res);
                exit_nicely(sql_conn);
            }
            PQclear(res);
        }		
        count_all_way++;
        if (count_all_way%1000 == 0) 
            fprintf(stderr, "\rProcessing: Way(%dk)", count_all_way/1000);

        xmlFree(xid);
    } else if (xmlStrEqual(name, BAD_CAST "seg")) {
        xid  = xmlTextReaderGetAttribute(reader, BAD_CAST "id");
        assert(xid);
        id   = strtoul((char *)xid, NULL, 10);

        if (addItem(&segs, "id", (char *)xid, 1)) {
            const char *way_id = getItem(&keys, "id");
            if (!way_id) way_id = "???";
            count_way_seg++;
        }
        DEBUG("\tSEG(%s)\n", xid);
        xmlFree(xid);
    } else if (xmlStrEqual(name, BAD_CAST "osm")) {
        /* ignore */
    } else {
        fprintf(stderr, "%s: Unknown element name: %s\n", __FUNCTION__, name);
    }
}

void EndElement(xmlTextReaderPtr reader, const xmlChar *name)
{
    int id;

    DEBUG("%s: %s\n", __FUNCTION__, name);

    if (xmlStrEqual(name, BAD_CAST "node")) {
        int i;
        char *values = NULL, *names = NULL;
        char *osm_id = getItem(&keys, "id");

        if (!osm_id) {
            fprintf(stderr, "%s: Node ID not in keys\n", __FUNCTION__);
            resetList(&keys);
            resetList(&tags);
            return;
        }
        id = strtoul(osm_id, NULL, 10);

        for (i=0; i < sizeof(exportTags) / sizeof(exportTags[0]); i++) {
            char *v;
            if ((v = getItem(&tags, exportTags[i].name))) {
                if (values) {
                    char *oldval = values, *oldnam = names;
                    asprintf(&names,  "%s,\"%s\"", oldnam, exportTags[i].name);
                    asprintf(&values, "%s,$$%s$$", oldval, v);
                    free(oldnam);
                    free(oldval);
                } else {
                    asprintf(&names,  "\"%s\"", exportTags[i].name);
                    asprintf(&values, "$$%s$$", v);
                }
            }
        }
        if (values) {
            char *sql = NULL;
            PGresult   *res;
            count_node++;
            asprintf(&sql, "INSERT INTO %s (osm_id,%s,way) values "
                    "(%s,%s,GeomFromText('POINT(%.15g %.15g)',4326));\n",
                    TABLE_NAME, names, osm_id, values, node_lon, node_lat);
            res = PQexec(sql_conn, sql);
            if (PQresultStatus(res) != PGRES_COMMAND_OK)
            {
                fprintf(stderr, "%s failed: %s", sql, PQerrorMessage(sql_conn));
                PQclear(res);
                exit_nicely(sql_conn);
            }
            PQclear(res);
            free(sql);
        }
        resetList(&keys);
        resetList(&tags);
        free(values);
        free(names);
    } else if (xmlStrEqual(name, BAD_CAST "segment")) {
        resetList(&tags);
    } else if (xmlStrEqual(name, BAD_CAST "tag")) {
        /* Separate tag list so tag stack unused */
    } else if (xmlStrEqual(name, BAD_CAST "way")) {
        int i, polygon = 0; 
        char *values = NULL, *names = NULL;
        char *osm_id = getItem(&keys, "id");

        if (!osm_id) {
            fprintf(stderr, "%s: WAY ID not in keys\n", __FUNCTION__);
            resetList(&keys);
            resetList(&tags);
            resetList(&segs);
            return;
        }

        if (!listHasData(&segs)) {
            DEBUG("%s: WAY(%s) has no segments\n", __FUNCTION__, osm_id);
            resetList(&keys);
            resetList(&tags);
            resetList(&segs);
            return;
        }
        id  = strtoul(osm_id, NULL, 10);

        for (i=0; i < sizeof(exportTags) / sizeof(exportTags[0]); i++) {
            char *v;
            if ((v = getItem(&tags, exportTags[i].name))) {
                if (values) {
                    char *oldval = values, *oldnam = names;
                    asprintf(&names,  "%s,\"%s\"", oldnam, exportTags[i].name);
                    asprintf(&values, "%s,$$%s$$", oldval, v);
                    free(oldnam);
                    free(oldval);
                } else {
                    asprintf(&names,  "\"%s\"", exportTags[i].name);
                    asprintf(&values, "$$%s$$", v);
                }
                polygon |= exportTags[i].polygon;
            }
        }
        if (values) {
            size_t wkt_size = WKT(polygon);

            if (wkt_size)
            {
                unsigned int i;
                for (i=0;i<wkt_size;i++)
                {
                    const char * wkt = get_wkt(i);
                    if (strlen(wkt)) {
                        char *sql = NULL;
                        PGresult   *res;
                        asprintf(&sql, "INSERT INTO %s (osm_id,%s,way) VALUES (%s,%s,GeomFromText('%s',4326));\n", TABLE_NAME,names,osm_id,values,wkt);
                        res = PQexec(sql_conn, sql);
                        if (PQresultStatus(res) != PGRES_COMMAND_OK)
                        {
                            fprintf(stderr, "%s failed: %s", sql, PQerrorMessage(sql_conn));
                            PQclear(res);
                            exit_nicely(sql_conn);
                        }
                        PQclear(res);
                        free(sql);
                        count_way++;
                    }
                }
                clear_wkts();
            }
        }

        resetList(&keys);
        resetList(&tags);
        resetList(&segs);
        free(values);
        free(names);
    } else if (xmlStrEqual(name, BAD_CAST "seg")) {
        /* ignore */
    } else if (xmlStrEqual(name, BAD_CAST "osm")) {
        /* ignore */
    } else {
        fprintf(stderr, "%s: Unknown element name: %s\n", __FUNCTION__, name);
    }
}

static void processNode(xmlTextReaderPtr reader) {
    xmlChar *name;
    name = xmlTextReaderName(reader);
    if (name == NULL)
        name = xmlStrdup(BAD_CAST "--");
	
    switch(xmlTextReaderNodeType(reader)) {
        case XML_READER_TYPE_ELEMENT:
            StartElement(reader, name);	
            if (xmlTextReaderIsEmptyElement(reader))
                EndElement(reader, name); /* No end_element for self closing tags! */
            break;
        case XML_READER_TYPE_END_ELEMENT:
            EndElement(reader, name);
            break;
        case XML_READER_TYPE_SIGNIFICANT_WHITESPACE:
            /* Ignore */
            break;
        default:
            fprintf(stderr, "Unknown node type %d\n", xmlTextReaderNodeType(reader));
            break;
    }

    xmlFree(name);
}

int streamFile(char *filename) {
    xmlTextReaderPtr reader;
    int ret = 0;

    reader = xmlNewTextReaderFilename(filename);
    if (reader != NULL) {
        ret = xmlTextReaderRead(reader);
        while (ret == 1) {
            processNode(reader);
            ret = xmlTextReaderRead(reader);
        }

        if (ret != 0) {
            fprintf(stderr, "%s : failed to parse\n", filename);
            return ret;
        }

        xmlFreeTextReader(reader);
    } else {
        fprintf(stderr, "Unable to open %s\n", filename);
    }
    return 0;
}


int main(int argc, char *argv[])
{
    char sql[1024], tmp[128];
    int i;
    PGresult   *res;

    if (argc != 2) {
        usage(argv[0]);
        exit(1);
    }


    initList(&keys);
    initList(&tags);
    initList(&segs);

    LIBXML_TEST_VERSION

    /* Make a connection to the database */
    sql_conn = PQconnectdb(conninfo);

    /* Check to see that the backend connection was successfully made */
    if (PQstatus(sql_conn) != CONNECTION_OK)
    {
        fprintf(stderr, "Connection to database failed: %s", PQerrorMessage(sql_conn));
        exit_nicely(sql_conn);
    }

    sql[0] = '\0';
    strcat(sql, "BEGIN;\n");

    strcat(sql, "CREATE TEMPORARY TABLE tmp_segments (\"id\" int4 PRIMARY KEY,\"from\" int4,\"to\" int4) ON COMMIT DROP;\n");
    strcat(sql, "PREPARE insert_segment (int4, int4, int4) AS INSERT INTO tmp_segments VALUES ($1,$2,$3);\n");
    strcat(sql, "PREPARE get_segment (int4) AS SELECT \"from\",\"to\" FROM tmp_segments WHERE \"id\" = $1 LIMIT 1;\n");

    strcat(sql, "CREATE TEMPORARY TABLE tmp_nodes (\"id\" int4 PRIMARY KEY, \"lat\" double precision, \"lon\" double precision) ON COMMIT DROP;\n");
    strcat(sql, "PREPARE insert_node (int4, double precision, double precision) AS INSERT INTO tmp_nodes VALUES ($1,$2,$3);\n");
    strcat(sql, "PREPARE get_node (int4) AS SELECT \"lat\",\"lon\" FROM tmp_nodes WHERE \"id\" = $1 LIMIT 1;\n");

    strcat(sql, "DROP TABLE " TABLE_NAME ";\n");
    strcat(sql, "CREATE TABLE " TABLE_NAME " ( osm_id int4");
    for (i=0; i < sizeof(exportTags) / sizeof(exportTags[0]); i++) {
        sprintf(tmp, ",\"%s\" %s", exportTags[i].name, exportTags[i].type);
        strcat(sql, tmp);
    }
    strcat(sql, " );\n");
    strcat(sql, "select AddGeometryColumn('" TABLE_NAME "', 'way', 4326, 'GEOMETRY', 2 );\n");

    res = PQexec(sql_conn, sql);
    if (PQresultStatus(res) != PGRES_TUPLES_OK)
    {
        fprintf(stderr, "%s failed: %s", sql, PQerrorMessage(sql_conn));
        PQclear(res);
        exit_nicely(sql_conn);
    }
    PQclear(res);

    if (streamFile(argv[1]) != 0)
        exit_nicely(sql_conn);

    xmlCleanupParser();
    xmlMemoryDump();

    sql[0] = '\0';
    strcat(sql, "COMMIT;\n");
    strcat(sql, "VACUUM ANALYZE " TABLE_NAME ";\n");
    strcat(sql, "CREATE INDEX way_index ON " TABLE_NAME " USING GIST (way GIST_GEOMETRY_OPS);\n");
    strcat(sql, "ALTER TABLE " TABLE_NAME " ALTER COLUMN way SET NOT NULL;\n");
    strcat(sql, "CLUSTER way_index on " TABLE_NAME ";\n");
    strcat(sql, "VACUUM ANALYZE " TABLE_NAME ";\n");

    res = PQexec(sql_conn, sql);
    if (PQresultStatus(res) != PGRES_COMMAND_OK)
    {
        fprintf(stderr, "%s failed: %s", sql, PQerrorMessage(sql_conn));
        PQclear(res);
        exit_nicely(sql_conn);
    }
    PQclear(res);

    fprintf(stderr, "\n");

    fprintf(stderr, "Node stats: out(%d), total(%d), max(%d)\n", count_node, count_all_node, max_node);
    fprintf(stderr, "Segment stats: out(%d), total(%d), max(%d)\n", count_segment, count_all_segment, max_segment);
    fprintf(stderr, "Way stats: out(%d), total(%d), max(%d)\n", count_way, count_all_way, max_way);
    fprintf(stderr, "Way stats: duplicate segments in ways %d\n", count_way_seg);

    PQfinish(sql_conn);

    return 0;
}
