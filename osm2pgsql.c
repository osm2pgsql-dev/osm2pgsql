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

#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <getopt.h>

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

int verbose;

static void printStatus(void)
{
    fprintf(stderr, "\rProcessing: Node(%dk) Segment(%dk) Way(%dk)",
            count_node/1000, count_segment/1000, count_way/1000);
}


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
            printStatus();

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

        count_segment++;
        if (count_segment%10000 == 0)
            printStatus();
 
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

        count_way++;
        if (count_way%1000 == 0)
            printStatus();

        xmlFree(xid);
    } else if (xmlStrEqual(name, BAD_CAST "seg")) {
        xid  = xmlTextReaderGetAttribute(reader, BAD_CAST "id");
        assert(xid);

        if (addItem(&segs, "id", (char *)xid, 1))
            count_way_seg++;

        xmlFree(xid);
    } else if (xmlStrEqual(name, BAD_CAST "osm")) {
        /* ignore */
    } else if (xmlStrEqual(name, BAD_CAST "bound")) {
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
        printStatus();
    } else if (xmlStrEqual(name, BAD_CAST "bound")) {
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
    const char *name = basename(arg0);

    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "\t%s [options] planet.osm\n", name);
    fprintf(stderr, "\t%s [options[ planet.osm.{gz,bz2}\n", name);
    fprintf(stderr, "\t%s [options] file1.osm file2.osm file3.osm\n", name);
    fprintf(stderr, "\nThis will import the data from the OSM file(s) into a PostgreSQL database\n");
    fprintf(stderr, "suitable for use by the Mapnik renderer\n");
    fprintf(stderr, "\nOptions:\n");
    fprintf(stderr, "   -a   --append\tAdd the OSM file into the database without removing\n");
    fprintf(stderr, "                \texisting data.\n");
    fprintf(stderr, "   -c   --create\tRemove existing data from the database. This is the \n");
    fprintf(stderr, "                \tdefault if --append is not specified.\n");
    fprintf(stderr, "   -d   --database\tThe name of the PostgreSQL database to connect\n");
    fprintf(stderr, "                  \tto (default: gis).\n");
    fprintf(stderr, "   -s   --slim\t\tStore temporary data in the database. This greatly\n");
    fprintf(stderr, "              \t\treduces the RAM usage but is much slower.\n");
    fprintf(stderr, "   -h   --help\t\tHelp information.\n");
    fprintf(stderr, "   -v   --verbose\tVerbose output.\n");
    fprintf(stderr, "\n");
}

int main(int argc, char *argv[])
{
    int append=0;
    int create=0;
    int slim=0;
    const char *db = "gis";

    fprintf(stderr, "osm2pgsql SVN version %s $Rev$ \n\n", VERSION);

    if (argc < 2) {
        usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    while (1) {
        int c, option_index = 0;
        static struct option long_options[] = {
            {"append",   0, 0, 'a'},
            {"create",   0, 0, 'c'},
            {"database", 1, 0, 'd'},
            {"verbose",  0, 0, 'v'},
            {"slim",     0, 0, 's'},
            {"help",     0, 0, 'h'},
            {0, 0, 0, 0}
        };

        c = getopt_long (argc, argv, "acd:hsv", long_options, &option_index);
        if (c == -1)
            break;

        switch (c) {
            case 'a': append=1;  break;
            case 'c': create=1;  break;
            case 'v': verbose=1; break;
            case 's': slim=1;    break;
            case 'd': db=optarg; break;

            case 'h':
            case '?':
            default:
                usage(argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    if (append && create) {
        fprintf(stderr, "Error: --append and --create options can not be used at the same time!\n");
        exit(EXIT_FAILURE);
    }

    text_init();
    initList(&tags);
    initList(&segs);

    LIBXML_TEST_VERSION

    project_init();

    mid = slim ? &mid_pgsql : &mid_ram;
    out = &out_pgsql;

    out->start(db, append);

    while (optind < argc) {
        fprintf(stderr, "\nReading in file: %s\n", argv[optind]);
        mid->start(db);
        if (streamFile(argv[optind]) != 0)
            exit_nicely();
        mid->end();
        mid->analyze();

        //mid->iterate_nodes(out->node);
        mid->iterate_ways(out->way);
        mid->stop();
        optind++;
    }

    xmlCleanupParser();
    xmlMemoryDump();

    if (count_node || count_segment || count_way) {
        fprintf(stderr, "\n");
        fprintf(stderr, "Node stats: total(%d), max(%d)\n", count_node, max_node);
        fprintf(stderr, "Segment stats: total(%d), max(%d)\n", count_segment, max_segment);
        fprintf(stderr, "Way stats: total(%d), max(%d)\n", count_way, max_way);
        //fprintf(stderr, "Way stats: duplicate segments in ways %d\n", count_way_seg);
    }
    //fprintf(stderr, "\n\nEnding data import\n");
    //out->process(mid);
    out->stop(append);

    project_exit();
    text_exit();
    fprintf(stderr, "\n");

    return 0;
}
