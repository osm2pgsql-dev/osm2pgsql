/*
  #-----------------------------------------------------------------------------
  # osm2pgsql - converts planet.osm file into PostgreSQL
  # compatible output suitable to be rendered by mapnik
  # Use: osm2pgsql planet.osm > planet.sql
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
   {"aeroway", "text", 0}
};

static const char *table_name_point = "planet_osm_point";
static const char *table_name_line = "planet_osm_line";
static const char *table_name_polygon = "planet_osm_polygon";

#define MAX_ID_NODE (35000000)
#define MAX_ID_SEGMENT (35000000)

struct osmNode {
      double lon;
      double lat;
};

struct osmSegment {
      unsigned int from;
      unsigned int to;
};

struct osmWay {
      char *values;
      char *wkt;
};

static struct osmNode    nodes[MAX_ID_NODE+1];
static struct osmSegment segments[MAX_ID_SEGMENT+1];

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


static struct keyval keys, tags, segs;


void usage(const char *arg0)
{
   fprintf(stderr, "Usage error:\n\t%s planet.osm  > planet.sql\n", arg0);
   fprintf(stderr, "or\n\tgzip -dc planet.osm.gz | %s - | gzip -c > planet.sql.gz\n", arg0);
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
         if (!strcmp(item->value, value) && !strcmp(item->key, name)) {
            //fprintf(stderr, "Discarded %s=%s\n", name, value);
            return 1;
         }
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
   while (listHasData(&segs))
   {
      struct keyval *p;
      unsigned int id, to, from;
      double x0, y0, x1, y1;
      p = popItem(&segs);
      id = strtoul(p->value, NULL, 10);
      freeItem(p);

      from = segments[id].from;
      to   = segments[id].to; 

      x0 = nodes[from].lon;
      y0 = nodes[from].lat;
      x1 = nodes[to].lon;
      y1 = nodes[to].lat;
      add_segment(x0,y0,x1,y1);
   }
   return  build_geometry(polygon);
}


void StartElement(xmlTextReaderPtr reader, const xmlChar *name)
{
   xmlChar *xid, *xlat, *xlon, *xfrom, *xto, *xk, *xv;
   unsigned int id, to, from;
   double lon, lat;
   char *k;

   if (xmlStrEqual(name, BAD_CAST "node")) {
      struct osmNode *node;
      xid  = xmlTextReaderGetAttribute(reader, BAD_CAST "id");
      xlon = xmlTextReaderGetAttribute(reader, BAD_CAST "lon");
      xlat = xmlTextReaderGetAttribute(reader, BAD_CAST "lat");
      assert(xid); assert(xlon); assert(xlat);
      id  = strtoul((char *)xid, NULL, 10);
      lon = strtod((char *)xlon, NULL);
      lat = strtod((char *)xlat, NULL);

      assert(id > 0);
      assert(id < MAX_ID_NODE);

      if (id > max_node) 
          max_node = id;

      count_all_node++;
      if (count_all_node%10000 == 0) 
         fprintf(stderr, "\rProcessing: Node(%dk)", count_all_node/1000);

      node = &nodes[id];
      node->lon = lon;
      node->lat = lat;

      DEBUG("NODE(%d) %f %f\n", id, lon, lat);
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

      assert(id > 0);
      assert(id < MAX_ID_SEGMENT);

      if (id > max_segment) 
          max_segment = id;

      if (count_all_segment == 0)
         fprintf(stderr, "\n");

      count_all_segment++;
      if (count_all_segment%10000 == 0) 
         fprintf(stderr, "\rProcessing: Segment(%dk)", count_all_segment/1000);

      if (!nodes[to].lat && !nodes[to].lon) {
         DEBUG("SEGMENT(%d), NODE(%d) is missing\n", id, to);
      } else if (!nodes[from].lat && !nodes[from].lon) {
         DEBUG("SEGMENT(%d), NODE(%d) is missing\n", id, from);
      } else {
         if (from != to) {
            struct osmSegment *segment;
            segment = &segments[id];
            segment->to   = to;
            segment->from = from;

            count_segment++;
            DEBUG("SEGMENT(%d) %d, %d\n", id, from, to);
         }
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

      if (count_all_way == 0)
         fprintf(stderr, "\n");
		
      count_all_way++;
      if (count_all_way%1000 == 0) 
         fprintf(stderr, "\rProcessing: Way(%dk)", count_all_way/1000);

      xmlFree(xid);
   } else if (xmlStrEqual(name, BAD_CAST "seg")) {
      xid  = xmlTextReaderGetAttribute(reader, BAD_CAST "id");
      assert(xid);
      id   = strtoul((char *)xid, NULL, 10);
      if (!id || (id > MAX_ID_SEGMENT))
         DEBUG("\tSEG(%s) - invalid segment ID\n", xid);
      else if (!segments[id].from || !segments[id].to)
         DEBUG("\tSEG(%s) - missing segment\n", xid);
      else {
         if (addItem(&segs, "id", (char *)xid, 1)) {
            const char *way_id = getItem(&keys, "id");
            if (!way_id) way_id = "???";
            //fprintf(stderr, "Way %s with duplicate segment id %d\n", way_id, id);
            count_way_seg++;
         }
         DEBUG("\tSEG(%s)\n", xid);
      }
      xmlFree(xid);
   } else if (xmlStrEqual(name, BAD_CAST "osm")) {
      /* ignore */
   } else {
      fprintf(stderr, "%s: Unknown element name: %s\n", __FUNCTION__, name);
   }
}

void EndElement(xmlTextReaderPtr reader, const xmlChar *name)
{
   unsigned int id;

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
      //assert(nodes[id].lat && nodes[id].lon);
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
         count_node++;
         printf("insert into %s (osm_id,%s,way) values "
		"(%s,%s,GeomFromText('POINT(%.15g %.15g)',4326));\n", 
		table_name_point,names,osm_id,values,nodes[id].lon, nodes[id].lat);
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
            unsigned i;
            for (i=0;i<wkt_size;i++)
            {
               const char * wkt = get_wkt(i);
               if (strlen(wkt)) {
		 if (polygon) {
                     printf("insert into %s (osm_id,%s,way) values (%s,%s,GeomFromText('%s',4326));\n", table_name_polygon,names,osm_id,values,wkt);
		 }
		 else {
		   printf("insert into %s (osm_id,%s,way) values (%s,%s,GeomFromText('%s',4326));\n", table_name_line,names,osm_id,values,wkt);
		 }
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

void streamFile(char *filename) {
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

   initList(&keys);
   initList(&tags);
   initList(&segs);

   LIBXML_TEST_VERSION

   printf("drop table %s ;\n", table_name_point);
   printf("create table %s ( osm_id int4",table_name_point);
   for (i=0; i < sizeof(exportTags) / sizeof(exportTags[0]); i++)
      printf(",\"%s\" %s", exportTags[i].name, exportTags[i].type);
   printf(" );\n");
   printf("select AddGeometryColumn('%s', 'way', 4326, 'POINT', 2 );\n", table_name_point);
   
   printf("drop table %s ;\n", table_name_line);
   printf("create table %s ( osm_id int4",table_name_line);
   for (i=0; i < sizeof(exportTags) / sizeof(exportTags[0]); i++)
      printf(",\"%s\" %s", exportTags[i].name, exportTags[i].type);
   printf(" );\n");
   printf("select AddGeometryColumn('%s', 'way', 4326, 'LINESTRING', 2 );\n", table_name_line);
   
   printf("drop table %s ;\n", table_name_polygon);
   printf("create table %s ( osm_id int4",table_name_polygon);
   for (i=0; i < sizeof(exportTags) / sizeof(exportTags[0]); i++)
      printf(",\"%s\" %s", exportTags[i].name, exportTags[i].type);
   printf(" );\n");
   printf("select AddGeometryColumn('%s', 'way', 4326, 'GEOMETRY', 2 );\n", table_name_polygon);
   
   printf("begin;\n");
   streamFile(argv[1]);
   printf("commit;\n");
   
   printf("vacuum analyze %s;\n", table_name_point);
   printf("vacuum analyze %s;\n", table_name_line);
   printf("vacuum analyze %s;\n", table_name_polygon);
   
   printf("CREATE INDEX way_index0 ON %s USING GIST (way GIST_GEOMETRY_OPS);\n", table_name_point);
   printf("ALTER TABLE %s ALTER COLUMN way SET NOT NULL;\n",table_name_point);
   printf("CLUSTER way_index0 on %s;\n",table_name_point);
   printf("vacuum analyze %s;\n", table_name_point);
   
   printf("CREATE INDEX way_index1 ON %s USING GIST (way GIST_GEOMETRY_OPS);\n", table_name_line);
   printf("ALTER TABLE %s ALTER COLUMN way SET NOT NULL;\n",table_name_line);
   printf("ALTER TABLE %s ADD COLUMN z_order int4 default 0;\n",table_name_line);
   printf("CLUSTER way_index1 on %s;\n",table_name_line);
   printf("vacuum analyze %s;\n", table_name_line);
   
   printf("CREATE INDEX way_index2 ON %s USING GIST (way GIST_GEOMETRY_OPS);\n", table_name_polygon);
   printf("ALTER TABLE %s ALTER COLUMN way SET NOT NULL;\n",table_name_polygon);
   printf("CLUSTER way_index2 on %s;\n",table_name_polygon);
   printf("vacuum analyze %s;\n", table_name_polygon);

   xmlCleanupParser();
   xmlMemoryDump();

   fprintf(stderr, "\n");

   fprintf(stderr, "Node stats: out(%d), total(%d), max(%d)\n", count_node, count_all_node, max_node);
   fprintf(stderr, "Segment stats: out(%d), total(%d), max(%d)\n", count_segment, count_all_segment, max_segment);
   fprintf(stderr, "Way stats: out(%d), total(%d), max(%d)\n", count_way, count_all_way, max_way);
   fprintf(stderr, "Way stats: duplicate segments in ways %d\n", count_way_seg);

   return 0;
}
