#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include <libxml/xmlreader.h>

#define WKT_MAX 128000
#define SQL_MAX 140000

#if 0
#define DEBUG printf
#else
#define DEBUG(x, ...) 
#endif

struct tagDesc {
	const char *name;
	const char *type;
}; 

static struct tagDesc exportTags[] = {
	       {"name","text"},
               {"place","text"},
               {"landuse","text"},
               {"leisure","text"},
               {"natural","text"},
               {"waterway","text"},
               {"highway","text"},
	       {"railway","text"},
               {"amenity","text"},
               {"tourism","text"},
               {"learning","text"}
};

//segments = {}
static const char *table_name = "planet_osm";
char fieldNames[128];

#define MAX_ID_NODE (25000000)
#define MAX_ID_SEGMENT (25000000)

struct osmNode {
	double lon;
	double lat;
};

struct osmSegment {
	unsigned long from;
	unsigned long to;
};

static struct osmNode    nodes[MAX_ID_NODE+1];
static struct osmSegment segments[MAX_ID_SEGMENT+1];

struct keyval;

struct keyval {
	char *key;
	char *value;
	struct keyval *next;
};

static struct keyval kvTail = { NULL, NULL, NULL };
static struct keyval *keys = &kvTail;
static struct keyval *tags = &kvTail;
static struct keyval *segs = &kvTail;

void usage(const char *arg0)
{
	fprintf(stderr, "Usage error:\n\t%s planet.osm  > planet.sql\n", arg0);
	fprintf(stderr, "or\n\tgzip -dc planet.osm.gz | %s - | gzip -c > planet.sql.gz\n", arg0);
}


void freeItem(struct keyval *p)
{
	free(p->key);
	free(p->value);
	free(p);
}


void resetList(struct keyval **list) 
{
	struct keyval *p = *list;
	struct keyval *next;
	
	while((next = p->next)) {		
		freeItem(p);
		p = next;
	}
	*list = p;	
}

char *getItem(struct keyval **list, const char *name)
{
	struct keyval *p = *list;
	while(p->next) {
		if (!strcmp(p->key, name))
			return p->value;
		p = p->next;
	}
	return NULL;
}	


struct keyval *popItem(struct keyval **list)
{
	struct keyval *p = *list;
	if (!p->next)
		return NULL;

	*list = p->next;
	return p;
}	


void pushItem(struct keyval **list, struct keyval *item)
{
	struct keyval *p = *list;
	struct keyval *q = NULL;
	
	/* TODO: Improve this inefficient algorithm to locate end of list
	 * e.g. cache tail or use double link list */
	while(p->next) {
		q = p;
		p = p->next;
	}

	item->next = p;
	if (q) 
		q->next = item;
	else 
		*list = item;
}	

void addItem(struct keyval **list, const char *name, const char *value)
{
	struct keyval *p = malloc(sizeof(struct keyval));
	
	if (!p) {
		fprintf(stderr, "Error allocating keyval\n");
		return;
	}
	p->key   = strdup(name);
	p->value = strdup(value);
	p->next = *list;
	*list = p;
}


void WKT(char *wkt, int polygon)
{
	struct keyval *p = segs;
	double start_x, start_y, end_x, end_y;
	int first = 1;
	char tmpwkt[WKT_MAX];
	int i = 0; 
	int max;
	wkt[0] = '\0';
	
	while(p->next) {
		i++;
		p = p->next;
	}
	max = i * i;

	i = 0;
	while (segs->next && i < max) {
		unsigned long id;
		int from, to;
		double x0, y0, x1, y1;

		p = popItem(&segs);
		id = strtoul(p->value, NULL, 10);
		from = segments[id].from;
		to   = segments[id].to; 
		i++;
		if (!from || !to) {
			freeItem(p);
			continue;
		}
		x0 = nodes[from].lon;
		y0 = nodes[from].lat;
		x1 = nodes[to].lon;
		y1 = nodes[to].lat;

		if (first) {
			first = 0;
			start_x = x0;
			start_y = y0;
			end_x = x1;
			end_y = y1;
			snprintf(wkt, WKT_MAX-1, "%.15g %.15g,%.15g %.15g", x0, y0, x1, y1);
		} else {
			strcpy(tmpwkt, wkt);
			if (start_x == x0 && start_y == y0)  {
				start_x = x1;
				start_y = y1;
				snprintf(wkt, WKT_MAX-1, "%.15g %.15g,%s", x1, y1, tmpwkt);
			} else if (start_x == x1 && start_y == y1)  {
				start_x = x0;
				start_y = y0;
				snprintf(wkt, WKT_MAX-1, "%.15g %.15g,%s", x0, y0, tmpwkt);
			} else if (end_x == x0 && end_y == y0)  {
				end_x = x1;
				end_y = y1;
				snprintf(wkt, WKT_MAX-1, "%s,%.15g %.15g", tmpwkt, x1, y1);
			} else if (end_x == x1 && end_y == y1)  {
				end_x = x0;
				end_y = y0;
				snprintf(wkt, WKT_MAX-1, "%s,%.15g %.15g", tmpwkt, x0, y0);
			} else {
				pushItem(&segs, p);
				continue;
			}
		}
		freeItem(p);
	}

	if (strlen(wkt)) {
		strcpy(tmpwkt, wkt);
		if (polygon) 
			snprintf(wkt, WKT_MAX-1, "POLYGON((%s,%.15g %.15g))", tmpwkt, start_x, start_y);
		else 
			snprintf(wkt, WKT_MAX-1, "LINESTRING(%s)", tmpwkt);
	}
}

void StartElement(xmlTextReaderPtr reader, const xmlChar *name)
{
	xmlChar *xid, *xlat, *xlon, *xfrom, *xto, *xk, *xv;
	unsigned long id, to, from;
	double lon, lat;
	char *k, *v;

	if (!strcmp(name, "node")) {
		xid  = xmlTextReaderGetAttribute(reader, "id");
		xlon = xmlTextReaderGetAttribute(reader, "lon");
		xlat = xmlTextReaderGetAttribute(reader, "lat");
		id  = strtoul(xid, NULL, 10);
		lon = strtod(xlon, NULL);
		lat = strtod(xlat, NULL);
		if (id > 0 && id < MAX_ID_NODE) {
			nodes[id].lon = lon;
			nodes[id].lat = lat;
			DEBUG("NODE(%d) %f %f\n", id, lon, lat);
			addItem(&keys, "id", xid);
		} else {
			fprintf(stderr, "%s: Invalid node ID %d (max %d)\n", __FUNCTION__, id, MAX_ID_NODE);
			exit(1);
		}
		xmlFree(xid);
		xmlFree(xlon);
		xmlFree(xlat);
	} else if (!strcmp(name, "segment")) {
		xid   = xmlTextReaderGetAttribute(reader, "id");
		xfrom = xmlTextReaderGetAttribute(reader, "from");
		xto   = xmlTextReaderGetAttribute(reader, "to");
		id   = strtoul(xid, NULL, 10);
		from = strtoul(xfrom, NULL, 10);
		to   = strtoul(xto, NULL, 10);
		if (id > 0 && id < MAX_ID_SEGMENT) {
			if (!nodes[to].lat && !nodes[to].lon) 
				DEBUG("SEGMENT(%d), NODE(%d) is missing\n", id, to);
			else if (!nodes[from].lat && !nodes[from].lon)
				DEBUG("SEGMENT(%d), NODE(%d) is missing\n", id, from);
			else {
				segments[id].to   = to;
				segments[id].from = from;
				DEBUG("SEGMENT(%d) %d, %d\n", id, from, to);
			}
		} else {
			fprintf(stderr, "%s: Invalid segment ID %d (max %d)\n", __FUNCTION__, id, MAX_ID_SEGMENT);
			exit(1);
		}
		xmlFree(xid);
		xmlFree(xfrom);
		xmlFree(xto);
	} else if (!strcmp(name, "tag")) {
		char *p;
		xk = xmlTextReaderGetAttribute(reader, "k");
		xv = xmlTextReaderGetAttribute(reader, "v");
		k  = xmlStrdup(xk);
		/* FIXME: This does not look safe on UTF-8 data */
		while ((p = strchr(k, ':')))
			*p = '_';
		while ((p = strchr(k, ' ')))
			*p = '_';
		addItem(&tags, k, xv);
		DEBUG("\t%s = %s\n", xk, xv);
		xmlFree(k);
		xmlFree(xk);
		xmlFree(xv);
	} else if (!strcmp(name, "way")) {
		xid  = xmlTextReaderGetAttribute(reader, "id");
		addItem(&keys, "id", xid);
		DEBUG("WAY(%s)\n", xid);
		xmlFree(xid);
	} else if (!strcmp(name, "seg")) {
		xid  = xmlTextReaderGetAttribute(reader, "id");
		id   = strtoul(xid, NULL, 10);
		if (!id || (id > MAX_ID_SEGMENT))
			DEBUG("\tSEG(%s) - invalid segment ID\n", xid);
		else if (!segments[id].to || !segments[id].from)
			DEBUG("\tSEG(%s) - empty segment\n", xid);
		else {
			addItem(&segs, "id", xid);
			DEBUG("\tSEG(%s)\n", xid);
		}
		xmlFree(xid);
	} else if (!strcmp(name, "osm")) {
		/* ignore */
	} else {
		fprintf(stderr, "%s: Unknown element name: %s\n", __FUNCTION__, name);
	}
}

void EndElement(xmlTextReaderPtr reader, const xmlChar *name)
{
	xmlChar *xid, *xlat, *xlon, *xfrom, *xto, *xk, *xv;
	unsigned long id, to, from;
	double lon, lat;
	char *k, *v;

	DEBUG("%s: %s\n", __FUNCTION__, name);

	if (!strcmp(name, "node")) {
		int i, count = 0; 
		char *values = NULL;
		char *osm_id = getItem(&keys, "id");
		if (!osm_id) {
			fprintf(stderr, "%s: Node ID not in keys\n", __FUNCTION__);
			resetList(&keys);
			resetList(&tags);
			return;
		}
		id  = strtoul(osm_id, NULL, 10);
		for (i=0; i < sizeof(exportTags) / sizeof(exportTags[0]); i++) {
			char *oldval = values;
			int width = strcmp(exportTags[i].name, "name")?32:64;
			if ((v = getItem(&tags, exportTags[i].name)))
				count++;
			else
				v = "";

			if (oldval)				
				asprintf(&values, "%s,$$%.*s$$", oldval, width, v);
			else
				asprintf(&values, "$$%.*s$$", width, v);

			free(oldval);
		}
		if (count) {
			char wkt[WKT_MAX], sql[SQL_MAX];
			snprintf(wkt, sizeof(wkt)-1, 
				"POINT(%.15g %.15g)", nodes[id].lon, nodes[id].lat);
			wkt[sizeof(wkt)-1] = '\0';
			snprintf(sql, sizeof(sql)-1, 
				"insert into %s (osm_id,%s,way) values (%s,%s,GeomFromText('%s',4326));", table_name,fieldNames,osm_id,values,wkt);
			printf("%s\n", sql);
		}
		resetList(&keys);
		resetList(&tags);
		free(values);
	} else if (!strcmp(name, "segment")) {
		resetList(&tags);
	} else if (!strcmp(name, "tag")) {
		/* Separate tag list so tag stack unused */
	} else if (!strcmp(name, "way")) {
		int i, polygon = 0; 
		char *values = NULL;
		char wkt[WKT_MAX], sql[SQL_MAX];
		char *osm_id = getItem(&keys, "id");

		if (!osm_id) {
			fprintf(stderr, "%s: WAY ID not in keys\n", __FUNCTION__);
			resetList(&keys);
			resetList(&tags);
			resetList(&segs);
			return;
		}
		if (!segs->next) {
			DEBUG(stderr, "%s: WAY(%s) has no segments\n", __FUNCTION__, osm_id);
			resetList(&keys);
			resetList(&tags);
			resetList(&segs);
			return;
		}
		id  = strtoul(osm_id, NULL, 10);
		for (i=0; i < sizeof(exportTags) / sizeof(exportTags[0]); i++) {
			char *oldval = values;
			const char *name = exportTags[i].name;
			if ((v = getItem(&tags, name))) {
				if (!strcmp(name, "landuse") || !strcmp(name, "leisure"))
					polygon = 1;
			} else
				v = "";			

			if (oldval)				
				asprintf(&values, "%s,$$%s$$", oldval, v);
			else
				asprintf(&values, "$$%s$$", v);

			free(oldval);
		}

		do {
			WKT(wkt, polygon); 
			if (strlen(wkt)) {
				snprintf(sql, sizeof(sql)-1, 
	               		"insert into %s (osm_id,%s,way) values (%s,%s,GeomFromText('%s',4326));", table_name,fieldNames,osm_id,values,wkt);
			        printf("%s\n", sql);
			}
        	} while (segs->next);
		resetList(&keys);
		resetList(&tags);
		resetList(&segs);
		free(values);
	} else if (!strcmp(name, "seg")) {
		/* ignore */
	} else if (!strcmp(name, "osm")) {
		/* ignore */
	} else {
		fprintf(stderr, "%s: Unknown element name: %s\n", __FUNCTION__, name);
	}
}

static void processNode(xmlTextReaderPtr reader) {
    xmlChar *name, *value;
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
    int ret;

    reader = xmlNewTextReaderFilename(filename);
    if (reader != NULL) {
        ret = xmlTextReaderRead(reader);
        while (ret == 1) {
            processNode(reader);
            ret = xmlTextReaderRead(reader);
        }

        if (ret != 0) {
            fprintf(stderr, "%s : failed to parse\n", filename);
		return;
        }

        xmlFreeTextReader(reader);
    } else {
        fprintf(stderr, "Unable to open %s\n", filename);
    }
}


int main(int argc, char *argv[])
{
	int i;

	if (argc != 2) {
		usage(argv[0]);
		exit(1);
	}
 
    /*
     * this initialize the library and check potential ABI mismatches
     * between the version it was compiled for and the actual shared
     * library used.
     */
    LIBXML_TEST_VERSION

    printf("drop table %s ;\n", table_name);
    printf("create table %s ( osm_id int4",table_name);
    fieldNames[0] = '\0';
    for (i=0; i < sizeof(exportTags) / sizeof(exportTags[0]); i++) {
	char tmp[32];
	sprintf(tmp, i?",%s":"%s", exportTags[i].name);
	strcat(fieldNames, tmp);
	printf(",%s %s", exportTags[i].name, exportTags[i].type);
    }	
    printf(" );\n");
    printf("select AddGeometryColumn('%s', 'way', 4326, 'GEOMETRY', 2 );\n", table_name);
    printf("begin;\n");

    streamFile(argv[1]);

    printf("commit;\n");
    printf("vacuum analyze %s;\n", table_name);
    printf("CREATE INDEX way_index ON %s USING GIST (way GIST_GEOMETRY_OPS);\n", table_name);
    printf("vacuum analyze %s;\n", table_name);

    /*
     * Cleanup function for the XML library.
     */
    xmlCleanupParser();
    /*
     * this is to debug memory for regression tests
     */
    xmlMemoryDump();

    return 0;
}
