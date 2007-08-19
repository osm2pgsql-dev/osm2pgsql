/*
#-----------------------------------------------------------------------------
# osm2pgsql - converts planet.osm file into PostgreSQL
# compatible output suitable to be rendered by mapnik
# Use: osm2pgsql planet.osm.bz2
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

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <libxml/xmlstring.h>
#include <libxml/xmlreader.h>

#include "osmtypes.h"
#include "build_geometry.h"
#include "keyvals.h"
#include "middle-pgsql.h"
#include "middle-ram.h"
#include "output-pgsql.h"
#include "sanitizer.h"
#include "reprojection.h"
#include "text-tree.h"

static int count_node,    max_node;
static int count_segment, max_segment;
static int count_way,     max_way;
static int count_way_seg;

struct middle_t *mid;
struct output_t *out;

/* Since {node,segment,way} elements are not nested we can guarantee the 
   values in an end tag must match those of the corresponding 
   start tag and can therefore be cached.
*/
static double node_lon, node_lat;
static int seg_to, seg_from;
static struct keyval tags, segs;
static int osm_id;

static int update;

void StartElement(xmlTextReaderPtr reader, const xmlChar *name)
{
    xmlChar *xid, *xlat, *xlon, *xfrom, *xto, *xk, *xv;
    char *k;

    if (xmlStrEqual(name, BAD_CAST "node")) {
        xid  = xmlTextReaderGetAttribute(reader, BAD_CAST "id");
        xlon = xmlTextReaderGetAttribute(reader, BAD_CAST "lon");
        xlat = xmlTextReaderGetAttribute(reader, BAD_CAST "lat");
        assert(xid); assert(xlon); assert(xlat);

        osm_id  = strtol((char *)xid, NULL, 10);
        node_lon = strtod((char *)xlon, NULL);
        node_lat = strtod((char *)xlat, NULL);

        if (osm_id > max_node) 
            max_node = osm_id;

        count_node++;
        if (count_node%10000 == 0) 
            fprintf(stderr, "\rProcessing: Node(%dk)", count_node/1000);

        xmlFree(xid);
        xmlFree(xlon);
        xmlFree(xlat);
    } else if (xmlStrEqual(name, BAD_CAST "segment")) {
        xid   = xmlTextReaderGetAttribute(reader, BAD_CAST "id");
        xfrom = xmlTextReaderGetAttribute(reader, BAD_CAST "from");
        xto   = xmlTextReaderGetAttribute(reader, BAD_CAST "to");
        assert(xid); assert(xfrom); assert(xto);
        osm_id = strtol((char *)xid, NULL, 10);
        seg_from = strtol((char *)xfrom, NULL, 10);
        seg_to   = strtol((char *)xto, NULL, 10);

        if (osm_id > max_segment) 
            max_segment = osm_id;

        if (count_segment == 0) 
            fprintf(stderr, "\n");

        count_segment++;
        if (count_segment%10000 == 0) 
            fprintf(stderr, "\rProcessing: Segment(%dk)", count_segment/1000);

        xmlFree(xid);
        xmlFree(xfrom);
        xmlFree(xto);
    } else if (xmlStrEqual(name, BAD_CAST "tag")) {
        xk = xmlTextReaderGetAttribute(reader, BAD_CAST "k");
        assert(xk);

        /* 'created_by' and 'source' are common and not interesting to mapnik renderer */
        if (strcmp((char *)xk, "created_by") && strcmp((char *)xk, "source")) {
            char *p;
            xv = xmlTextReaderGetAttribute(reader, BAD_CAST "v");
            assert(xv);
            k  = (char *)xmlStrdup(xk);
            while ((p = strchr(k, ' ')))
                *p = '_';

            addItem(&tags, k, (char *)xv, 0);
            xmlFree(k);
            xmlFree(xv);
        }
        xmlFree(xk);
    } else if (xmlStrEqual(name, BAD_CAST "way")) {
        xid  = xmlTextReaderGetAttribute(reader, BAD_CAST "id");
        assert(xid);
        osm_id   = strtol((char *)xid, NULL, 10);

        if (osm_id > max_way)
            max_way = osm_id;

        if (count_way == 0)
            fprintf(stderr, "\n");

        count_way++;
        if (count_way%1000 == 0) 
            fprintf(stderr, "\rProcessing: Way(%dk)", count_way/1000);

        xmlFree(xid);
    } else if (xmlStrEqual(name, BAD_CAST "seg")) {
        xid  = xmlTextReaderGetAttribute(reader, BAD_CAST "id");
        assert(xid);

        if (addItem(&segs, "id", (char *)xid, 1))
            count_way_seg++;

        xmlFree(xid);
    } else if (xmlStrEqual(name, BAD_CAST "osm")) {
        /* ignore */
    } else {
        fprintf(stderr, "%s: Unknown element name: %s\n", __FUNCTION__, name);
    }
}

void EndElement(const xmlChar *name)
{
    if (xmlStrEqual(name, BAD_CAST "node")) {
       reproject(&node_lat, &node_lon);
       mid->nodes_set(osm_id, node_lat, node_lon, &tags);
        resetList(&tags);
    } else if (xmlStrEqual(name, BAD_CAST "segment")) {
        mid->segments_set(osm_id, seg_from, seg_to, &tags);
        resetList(&tags);
    } else if (xmlStrEqual(name, BAD_CAST "way")) {
        mid->ways_set(osm_id, &segs, &tags);
        resetList(&tags);
        resetList(&segs);
    } else if (xmlStrEqual(name, BAD_CAST "tag")) {
        /* ignore */
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
                EndElement(name); /* No end_element for self closing tags! */
            break;
        case XML_READER_TYPE_END_ELEMENT:
            EndElement(name);
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

static int streamFile(char *filename) {
    xmlTextReaderPtr reader;
    int ret = 0;

//    reader = xmlNewTextReaderFilename(filename);
    reader = sanitizerOpen(filename);

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
        return 1;
    }
    return 0;
}

void exit_nicely(void)
{
    fprintf(stderr, "Error occurred, cleaning up\n");
    out->cleanup();
    mid->cleanup();
    exit(1);
}
 
static void usage(const char *arg0)
{
    fprintf(stderr, "Usage error:\n\t%s planet.osm\n", arg0);
    fprintf(stderr, "\nor read a .bzip2 or .gz file directly\n\t%s planet.osm.bz2\n", arg0);
    fprintf(stderr, "\nor use 7za to decompress and pipe the data in\n\t7za x -so ~/osm/planet/planet-070516.osm.7z | %s -\n", arg0);
}

int main(int argc, char *argv[])
{
    fprintf(stderr, "osm2pgsql SVN version %s $Rev$ \n\n", VERSION);

    if (argc != 2) {
        usage(argv[0]);
        exit(1);
    }

    // use getopt
    update = 0;

    text_init();
    initList(&tags);
    initList(&segs);

    LIBXML_TEST_VERSION

    project_init();

    //mid = &mid_pgsql;
    mid = &mid_ram;
    out = &out_pgsql;

    mid->start(!update);
    out->start(!update);

    if (streamFile(argv[1]) != 0)
        exit_nicely();

    xmlCleanupParser();
    xmlMemoryDump();

    fprintf(stderr, "\n");
    fprintf(stderr, "Node stats: total(%d), max(%d)\n", count_node, max_node);
    fprintf(stderr, "Segment stats: total(%d), max(%d)\n", count_segment, max_segment);
    fprintf(stderr, "Way stats: total(%d), max(%d)\n", count_way, max_way);
    fprintf(stderr, "Way stats: duplicate segments in ways %d\n", count_way_seg);

    fprintf(stderr, "\n\nEnding data import\n");
    mid->end();

    fprintf(stderr, "\n\nRunning analysis on intermediate data\n");
    mid->analyze();

    fprintf(stderr, "\n\nOutput processing\n");

    //mid->iterate_nodes(out->node);
    mid->iterate_ways(out->way);

    //out->process(mid);
    mid->stop();
    out->stop();

    project_exit();

    fprintf(stderr, "\n");

    return 0;
}
