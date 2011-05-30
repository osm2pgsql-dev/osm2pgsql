/*
#-----------------------------------------------------------------------------
# osm2pgsql - converts planet.osm file into PostgreSQL
# compatible output suitable to be rendered by mapnik
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

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <libxml/xmlstring.h>
#include <libxml/xmlreader.h>

#include "osmtypes.h"
#include "sanitizer.h"
#include "reprojection.h"
#include "input.h"
#include "output.h"

#include "parse-xml2.h"



/* Parses the action="foo" tags in JOSM change files. Obvisouly not useful from osmChange files */
static actions_t ParseAction( xmlTextReaderPtr reader, struct osmdata_t *osmdata )
{
    if( osmdata->filetype == FILETYPE_OSMCHANGE || osmdata->filetype == FILETYPE_PLANETDIFF )
        return osmdata->action;
    actions_t new_action = ACTION_NONE;
    xmlChar *action = xmlTextReaderGetAttribute( reader, BAD_CAST "action" );
    if( action == NULL )
        new_action = ACTION_CREATE;
    else if( strcmp((char *)action, "modify") == 0 )
        new_action = ACTION_MODIFY;
    else if( strcmp((char *)action, "delete") == 0 )
        new_action = ACTION_DELETE;
    else
    {
        fprintf( stderr, "Unknown value for action: %s\n", (char*)action );
        exit_nicely();
    }
    return new_action;
}

static void StartElement(xmlTextReaderPtr reader, const xmlChar *name, struct osmdata_t *osmdata)
{
    xmlChar *xid, *xlat, *xlon, *xk, *xv, *xrole, *xtype;
    char *k;

    if (osmdata->filetype == FILETYPE_NONE)
    {
        if (xmlStrEqual(name, BAD_CAST "osm"))
        {
            osmdata->filetype = FILETYPE_OSM;
            osmdata->action = ACTION_CREATE;
        }
        else if (xmlStrEqual(name, BAD_CAST "osmChange"))
        {
            osmdata->filetype = FILETYPE_OSMCHANGE;
            osmdata->action = ACTION_NONE;
        }
        else if (xmlStrEqual(name, BAD_CAST "planetdiff"))
        {
            osmdata->filetype = FILETYPE_PLANETDIFF;
            osmdata->action = ACTION_NONE;
        }
        else
        {
            fprintf( stderr, "Unknown XML document type: %s\n", name );
            exit_nicely();
        }
        return;
    }
    
    if (xmlStrEqual(name, BAD_CAST "node")) {
        xid  = xmlTextReaderGetAttribute(reader, BAD_CAST "id");
        assert(xid); 
        osmdata->osm_id   = strtol((char *)xid, NULL, 10);
        osmdata->action   = ParseAction( reader , osmdata);
        
        if (osmdata->action != ACTION_DELETE) {
            xlon = xmlTextReaderGetAttribute(reader, BAD_CAST "lon");
            xlat = xmlTextReaderGetAttribute(reader, BAD_CAST "lat");
            assert(xlon); 
            assert(xlat);
            osmdata->node_lon = strtod((char *)xlon, NULL);
            osmdata->node_lat = strtod((char *)xlat, NULL);
        }

        if (osmdata->osm_id > osmdata->max_node)
            osmdata->max_node = osmdata->osm_id;

        osmdata->count_node++;
        if (osmdata->count_node%10000 == 0)
            printStatus(osmdata);

        xmlFree(xid);
        xmlFree(xlon);
        xmlFree(xlat);
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

            addItem(&(osmdata->tags), k, (char *)xv, 0);
            xmlFree(k);
            xmlFree(xv);
        }
        xmlFree(xk);
    } else if (xmlStrEqual(name, BAD_CAST "way")) {

        xid  = xmlTextReaderGetAttribute(reader, BAD_CAST "id");
        assert(xid);
        osmdata->osm_id   = strtol((char *)xid, NULL, 10);
        osmdata->action = ParseAction( reader, osmdata );
	
        if (osmdata->osm_id > osmdata->max_way)
	  osmdata->max_way = osmdata->osm_id;
	
        osmdata->count_way++;
        if (osmdata->count_way%1000 == 0)
	  printStatus(osmdata);
	
        osmdata->nd_count = 0;
        xmlFree(xid);

    } else if (xmlStrEqual(name, BAD_CAST "nd")) {
        xid  = xmlTextReaderGetAttribute(reader, BAD_CAST "ref");
        assert(xid);

        osmdata->nds[osmdata->nd_count++] = strtol( (char *)xid, NULL, 10 );

        if( osmdata->nd_count >= osmdata->nd_max )
          realloc_nodes(osmdata);
        xmlFree(xid);
    } else if (xmlStrEqual(name, BAD_CAST "relation")) {
        xid  = xmlTextReaderGetAttribute(reader, BAD_CAST "id");
        assert(xid);
        osmdata->osm_id   = strtol((char *)xid, NULL, 10);
        osmdata->action = ParseAction( reader, osmdata );

        if (osmdata->osm_id > osmdata->max_rel)
            osmdata->max_rel = osmdata->osm_id;

        osmdata->count_rel++;
        if (osmdata->count_rel%10 == 0)
            printStatus(osmdata);

        osmdata->member_count = 0;
        xmlFree(xid);
    } else if (xmlStrEqual(name, BAD_CAST "member")) {
	xrole = xmlTextReaderGetAttribute(reader, BAD_CAST "role");
	assert(xrole);

	xtype = xmlTextReaderGetAttribute(reader, BAD_CAST "type");
	assert(xtype);

        xid  = xmlTextReaderGetAttribute(reader, BAD_CAST "ref");
        assert(xid);

        osmdata->members[osmdata->member_count].id   = strtol( (char *)xid, NULL, 0 );
        osmdata->members[osmdata->member_count].role = strdup( (char *)xrole );
        
        /* Currently we are only interested in 'way' members since these form polygons with holes */
	if (xmlStrEqual(xtype, BAD_CAST "way"))
	    osmdata->members[osmdata->member_count].type = OSMTYPE_WAY;
	if (xmlStrEqual(xtype, BAD_CAST "node"))
	    osmdata->members[osmdata->member_count].type = OSMTYPE_NODE;
	if (xmlStrEqual(xtype, BAD_CAST "relation"))
	    osmdata->members[osmdata->member_count].type = OSMTYPE_RELATION;
        osmdata->member_count++;

        if( osmdata->member_count >= osmdata->member_max )
          realloc_members(osmdata);
        xmlFree(xid);
        xmlFree(xrole);
        xmlFree(xtype);
    } else if (xmlStrEqual(name, BAD_CAST "add") ||
               xmlStrEqual(name, BAD_CAST "create")) {
        osmdata->action = ACTION_MODIFY; // Turns all creates into modifies, makes it resiliant against inconsistant snapshots.
    } else if (xmlStrEqual(name, BAD_CAST "modify")) {
        osmdata->action = ACTION_MODIFY;
    } else if (xmlStrEqual(name, BAD_CAST "delete")) {
        osmdata->action = ACTION_DELETE;
    } else if (xmlStrEqual(name, BAD_CAST "bound")) {
        /* ignore */
    } else if (xmlStrEqual(name, BAD_CAST "bounds")) {
        /* ignore */
    } else if (xmlStrEqual(name, BAD_CAST "changeset")) {
        /* ignore */
    } else {
        fprintf(stderr, "%s: Unknown element name: %s\n", __FUNCTION__, name);
    }

    // Collect extra attribute information and add as tags
    if (osmdata->extra_attributes && (xmlStrEqual(name, BAD_CAST "node") ||
				      xmlStrEqual(name, BAD_CAST "way") ||
				      xmlStrEqual(name, BAD_CAST "relation")))
    {
        xmlChar *xtmp;

        xtmp = xmlTextReaderGetAttribute(reader, BAD_CAST "user");
        if (xtmp) {
	    addItem(&(osmdata->tags), "osm_user", (char *)xtmp, 0);
            xmlFree(xtmp);
        }

        xtmp = xmlTextReaderGetAttribute(reader, BAD_CAST "uid");
        if (xtmp) {
	    addItem(&(osmdata->tags), "osm_uid", (char *)xtmp, 0);
            xmlFree(xtmp);
        }

        xtmp = xmlTextReaderGetAttribute(reader, BAD_CAST "version");
        if (xtmp) {
	    addItem(&(osmdata->tags), "osm_version", (char *)xtmp, 0);
            xmlFree(xtmp);
        }

        xtmp = xmlTextReaderGetAttribute(reader, BAD_CAST "timestamp");
        if (xtmp) {
	    addItem(&(osmdata->tags), "osm_timestamp", (char *)xtmp, 0);
            xmlFree(xtmp);
        }
    }
}


static void EndElement(const xmlChar *name, struct osmdata_t *osmdata)
{
    if (xmlStrEqual(name, BAD_CAST "node")) {
      if (node_wanted(osmdata, osmdata->node_lat, osmdata->node_lon)) {
	  reproject(&(osmdata->node_lat), &(osmdata->node_lon));
            if( osmdata->action == ACTION_CREATE )
	        osmdata->out->node_add(osmdata->osm_id, osmdata->node_lat, osmdata->node_lon, &(osmdata->tags));
            else if( osmdata->action == ACTION_MODIFY )
	        osmdata->out->node_modify(osmdata->osm_id, osmdata->node_lat, osmdata->node_lon, &(osmdata->tags));
            else if( osmdata->action == ACTION_DELETE )
                osmdata->out->node_delete(osmdata->osm_id);
            else
            {
                fprintf( stderr, "Don't know action for node %d\n", osmdata->osm_id );
                exit_nicely();
            }
        }
        resetList(&(osmdata->tags));
    } else if (xmlStrEqual(name, BAD_CAST "way")) {
        if( osmdata->action == ACTION_CREATE )
	    osmdata->out->way_add(osmdata->osm_id, osmdata->nds, osmdata->nd_count, &(osmdata->tags) );
        else if( osmdata->action == ACTION_MODIFY )
	    osmdata->out->way_modify(osmdata->osm_id, osmdata->nds, osmdata->nd_count, &(osmdata->tags) );
        else if( osmdata->action == ACTION_DELETE )
            osmdata->out->way_delete(osmdata->osm_id);
        else
        {
            fprintf( stderr, "Don't know action for way %d\n", osmdata->osm_id );
            exit_nicely();
        }
        resetList(&(osmdata->tags));
    } else if (xmlStrEqual(name, BAD_CAST "relation")) {
        if( osmdata->action == ACTION_CREATE )
	    osmdata->out->relation_add(osmdata->osm_id, osmdata->members, osmdata->member_count, &(osmdata->tags));
        else if( osmdata->action == ACTION_MODIFY )
	    osmdata->out->relation_modify(osmdata->osm_id, osmdata->members, osmdata->member_count, &(osmdata->tags));
        else if( osmdata->action == ACTION_DELETE )
            osmdata->out->relation_delete(osmdata->osm_id);
        else
        {
            fprintf( stderr, "Don't know action for relation %d\n", osmdata->osm_id );
            exit_nicely();
        }
        resetList(&(osmdata->tags));
        resetMembers(osmdata);
    } else if (xmlStrEqual(name, BAD_CAST "tag")) {
        /* ignore */
    } else if (xmlStrEqual(name, BAD_CAST "nd")) {
        /* ignore */
    } else if (xmlStrEqual(name, BAD_CAST "member")) {
	/* ignore */
    } else if (xmlStrEqual(name, BAD_CAST "osm")) {
        printStatus(osmdata);
        osmdata->filetype = FILETYPE_NONE;
    } else if (xmlStrEqual(name, BAD_CAST "osmChange")) {
        printStatus(osmdata);
        osmdata->filetype = FILETYPE_NONE;
    } else if (xmlStrEqual(name, BAD_CAST "planetdiff")) {
        printStatus(osmdata);
        osmdata->filetype = FILETYPE_NONE;
    } else if (xmlStrEqual(name, BAD_CAST "bound")) {
        /* ignore */
    } else if (xmlStrEqual(name, BAD_CAST "bounds")) {
        /* ignore */
    } else if (xmlStrEqual(name, BAD_CAST "changeset")) {
        /* ignore */
	resetList(&(osmdata->tags)); /* We may have accumulated some tags even if we ignored the changeset */
    } else if (xmlStrEqual(name, BAD_CAST "add")) {
        osmdata->action = ACTION_NONE;
    } else if (xmlStrEqual(name, BAD_CAST "create")) {
        osmdata->action = ACTION_NONE;
    } else if (xmlStrEqual(name, BAD_CAST "modify")) {
        osmdata->action = ACTION_NONE;
    } else if (xmlStrEqual(name, BAD_CAST "delete")) {
        osmdata->action = ACTION_NONE;
    } else {
        fprintf(stderr, "%s: Unknown element name: %s\n", __FUNCTION__, name);
    }
}

static void processNode(xmlTextReaderPtr reader, struct osmdata_t *osmdata) {
    xmlChar *name;
    name = xmlTextReaderName(reader);
    if (name == NULL)
        name = xmlStrdup(BAD_CAST "--");
	
    switch(xmlTextReaderNodeType(reader)) {
        case XML_READER_TYPE_ELEMENT:
	    StartElement(reader, name, osmdata);
            if (xmlTextReaderIsEmptyElement(reader))
	        EndElement(name, osmdata); /* No end_element for self closing tags! */
            break;
        case XML_READER_TYPE_END_ELEMENT:
	    EndElement(name, osmdata);
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

int streamFileXML2(char *filename, int sanitize, struct osmdata_t *osmdata) {
    xmlTextReaderPtr reader;
    int ret = 0;

    if (sanitize)
        reader = sanitizerOpen(filename);
    else
        reader = inputUTF8(filename);

    if (reader != NULL) {
        ret = xmlTextReaderRead(reader);
        while (ret == 1) {
	  processNode(reader, osmdata);
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

