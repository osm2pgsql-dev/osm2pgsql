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

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>

#include "sanitizer.hpp"
#include "input.hpp"

#include "parse-xml2.hpp"
#include "output.hpp"
#include "util.hpp"

/* Parses the action="foo" tags in JOSM change files. Obvisouly not useful from osmChange files */
actions_t parse_xml2_t::ParseAction( xmlTextReaderPtr reader)
{
    actions_t new_action;
    xmlChar *action_text;
    if( filetype == FILETYPE_OSMCHANGE || filetype == FILETYPE_PLANETDIFF )
        return action;
    new_action = ACTION_NONE;
    action_text = xmlTextReaderGetAttribute( reader, BAD_CAST "action" );
    if( action_text == NULL )
        new_action = ACTION_CREATE;
    else if( strcmp((char *)action_text, "modify") == 0 )
        new_action = ACTION_MODIFY;
    else if( strcmp((char *)action_text, "delete") == 0 )
        new_action = ACTION_DELETE;
    else
    {
        fprintf( stderr, "Unknown value for action: %s\n", (char*)action_text );
        util::exit_nicely();
    }
    return new_action;
}

void parse_xml2_t::SetFiletype(const xmlChar* name, osmdata_t* osmdata)
{
	if (xmlStrEqual(name, BAD_CAST "osm"))
	{
		filetype = FILETYPE_OSM;
		action = ACTION_CREATE;
	}
	else if (xmlStrEqual(name, BAD_CAST "osmChange"))
	{
		filetype = FILETYPE_OSMCHANGE;
		action = ACTION_NONE;
	}
	else if (xmlStrEqual(name, BAD_CAST "planetdiff"))
	{
		filetype = FILETYPE_PLANETDIFF;
		action = ACTION_NONE;
	}
	else
	{
		fprintf( stderr, "Unknown XML document type: %s\n", name );
		util::exit_nicely();
	}
}

void parse_xml2_t::StartElement(xmlTextReaderPtr reader, const xmlChar *name, struct osmdata_t *osmdata)
{
  xmlChar *xid, *xlat, *xlon, *xk, *xv, *xrole, *xtype;
  char *k;

  //first time in we figure out what kind of data this is
  if (filetype == FILETYPE_NONE)
  {
      SetFiletype(name, osmdata);
      return;
  }

  //remember which this was for collecting tags at the end
  bool can_have_attribs = false;

  if (xmlStrEqual(name, BAD_CAST "node")) {
    can_have_attribs = true;

    xid  = xmlTextReaderGetAttribute(reader, BAD_CAST "id");
    xlon = xmlTextReaderGetAttribute(reader, BAD_CAST "lon");
    xlat = xmlTextReaderGetAttribute(reader, BAD_CAST "lat");
    assert(xid);

    osm_id   = strtoosmid((char *)xid, NULL, 10);
    action   = ParseAction(reader);

    if (action != ACTION_DELETE) {
      assert(xlon); assert(xlat);
      node_lon = strtod((char *)xlon, NULL);
      node_lat = strtod((char *)xlat, NULL);
    }

    if (osm_id > max_node)
      max_node = osm_id;

    if (count_node == 0) {
      time(&start_node);
    }
    count_node++;
    if (count_node%10000 == 0)
      printStatus();

    xmlFree(xid);
    xmlFree(xlon);
    xmlFree(xlat);
  } else if (xmlStrEqual(name, BAD_CAST "way")) {
    can_have_attribs = true;

    xid  = xmlTextReaderGetAttribute(reader, BAD_CAST "id");
    assert(xid);
    osm_id   = strtoosmid((char *)xid, NULL, 10);
    action = ParseAction( reader );

    if (osm_id > max_way)
      max_way = osm_id;

    if (count_way == 0) {
      time(&start_way);
    }
    count_way++;
    if (count_way%1000 == 0)
      printStatus();

    nd_count = 0;
    xmlFree(xid);

  } else if (xmlStrEqual(name, BAD_CAST "relation")) {
    can_have_attribs = true;

    xid  = xmlTextReaderGetAttribute(reader, BAD_CAST "id");
    assert(xid);
    osm_id   = strtoosmid((char *)xid, NULL, 10);
    action = ParseAction( reader );

    if (osm_id > max_rel)
      max_rel = osm_id;

    if (count_rel == 0) {
      time(&start_rel);
    }
    count_rel++;
    if (count_rel%10 == 0)
      printStatus();

    member_count = 0;
    xmlFree(xid);
  } else if (xmlStrEqual(name, BAD_CAST "tag")) {
    xk = xmlTextReaderGetAttribute(reader, BAD_CAST "k");
    assert(xk);

    char *p;
    xv = xmlTextReaderGetAttribute(reader, BAD_CAST "v");
    assert(xv);
    k  = (char *)xmlStrdup(xk);
    while ((p = strchr(k, ' ')))
      *p = '_';

    tags.addItem(k, (char *)xv, 0);
    xmlFree(k);
    xmlFree(xv);
    xmlFree(xk);
  } else if (xmlStrEqual(name, BAD_CAST "nd")) {
      xid  = xmlTextReaderGetAttribute(reader, BAD_CAST "ref");
      assert(xid);

      nds[nd_count++] = strtoosmid( (char *)xid, NULL, 10 );

      if( nd_count >= nd_max )
        realloc_nodes();
      xmlFree(xid);
  } else if (xmlStrEqual(name, BAD_CAST "member")) {
    xrole = xmlTextReaderGetAttribute(reader, BAD_CAST "role");
    assert(xrole);

    xtype = xmlTextReaderGetAttribute(reader, BAD_CAST "type");
    assert(xtype);

    xid = xmlTextReaderGetAttribute(reader, BAD_CAST "ref");
    assert(xid);

    members[member_count].id = strtoosmid((char *) xid, NULL, 0);
    members[member_count].role = strdup((char *) xrole);

    /* Currently we are only interested in 'way' members since these form polygons with holes */
    if (xmlStrEqual(xtype, BAD_CAST "way"))
      members[member_count].type = OSMTYPE_WAY;
    if (xmlStrEqual(xtype, BAD_CAST "node"))
      members[member_count].type = OSMTYPE_NODE;
    if (xmlStrEqual(xtype, BAD_CAST "relation"))
      members[member_count].type = OSMTYPE_RELATION;
    member_count++;

    if (member_count >= member_max)
      realloc_members();
    xmlFree(xid);
    xmlFree(xrole);
    xmlFree(xtype);
  } else if (xmlStrEqual(name, BAD_CAST "add") ||
             xmlStrEqual(name, BAD_CAST "create")) {
      action = ACTION_MODIFY; /* Turns all creates into modifies, makes it resiliant against inconsistant snapshots. */
  } else if (xmlStrEqual(name, BAD_CAST "modify")) {
      action = ACTION_MODIFY;
  } else if (xmlStrEqual(name, BAD_CAST "delete")) {
      action = ACTION_DELETE;
  } else if (xmlStrEqual(name, BAD_CAST "bound")) {
      /* ignore */
  } else if (xmlStrEqual(name, BAD_CAST "bounds")) {
      /* ignore */
  } else if (xmlStrEqual(name, BAD_CAST "changeset")) {
      /* ignore */
  } else {
      fprintf(stderr, "%s: Unknown element name: %s\n", __FUNCTION__, name);
  }

  /* Collect extra attribute information and add as tags */
  if (extra_attributes && can_have_attribs)
  {
      xmlChar *xtmp;

      xtmp = xmlTextReaderGetAttribute(reader, BAD_CAST "user");
      if (xtmp) {
          tags.addItem("osm_user", (char *)xtmp, false);
          xmlFree(xtmp);
      }

      xtmp = xmlTextReaderGetAttribute(reader, BAD_CAST "uid");
      if (xtmp) {
          tags.addItem("osm_uid", (char *)xtmp, false);
          xmlFree(xtmp);
      }

      xtmp = xmlTextReaderGetAttribute(reader, BAD_CAST "version");
      if (xtmp) {
          tags.addItem("osm_version", (char *)xtmp, false);
          xmlFree(xtmp);
      }

      xtmp = xmlTextReaderGetAttribute(reader, BAD_CAST "timestamp");
      if (xtmp) {
          tags.addItem("osm_timestamp", (char *)xtmp, false);
          xmlFree(xtmp);
      }

      xtmp = xmlTextReaderGetAttribute(reader, BAD_CAST "changeset");
      if (xtmp) {
          tags.addItem("osm_changeset", (char *)xtmp, false);
          xmlFree(xtmp);
      }
  }
}


void parse_xml2_t::EndElement(const xmlChar *name, struct osmdata_t *osmdata)
{
    if (xmlStrEqual(name, BAD_CAST "node")) {
      if (node_wanted(node_lat, node_lon)) {
	  proj->reproject(&(node_lat), &(node_lon));
            if( action == ACTION_CREATE )
	        osmdata->node_add(osm_id, node_lat, node_lon, &(tags));
            else if( action == ACTION_MODIFY )
	        osmdata->node_modify(osm_id, node_lat, node_lon, &(tags));
            else if( action == ACTION_DELETE )
                osmdata->node_delete(osm_id);
            else
            {
                fprintf( stderr, "Don't know action for node %" PRIdOSMID "\n", osm_id );
                util::exit_nicely();
            }
        }
        tags.resetList();
    } else if (xmlStrEqual(name, BAD_CAST "way")) {
        if( action == ACTION_CREATE )
	    osmdata->way_add(osm_id, nds, nd_count, &(tags) );
        else if( action == ACTION_MODIFY )
	    osmdata->way_modify(osm_id, nds, nd_count, &(tags) );
        else if( action == ACTION_DELETE )
            osmdata->way_delete(osm_id);
        else
        {
            fprintf( stderr, "Don't know action for way %" PRIdOSMID "\n", osm_id );
            util::exit_nicely();
        }
        tags.resetList();
    } else if (xmlStrEqual(name, BAD_CAST "relation")) {
        if( action == ACTION_CREATE )
	    osmdata->relation_add(osm_id, members, member_count, &(tags));
        else if( action == ACTION_MODIFY )
	    osmdata->relation_modify(osm_id, members, member_count, &(tags));
        else if( action == ACTION_DELETE )
            osmdata->relation_delete(osm_id);
        else
        {
            fprintf( stderr, "Don't know action for relation %" PRIdOSMID "\n", osm_id );
            util::exit_nicely();
        }
        tags.resetList();
        resetMembers();
    } else if (xmlStrEqual(name, BAD_CAST "tag")) {
        /* ignore */
    } else if (xmlStrEqual(name, BAD_CAST "nd")) {
        /* ignore */
    } else if (xmlStrEqual(name, BAD_CAST "member")) {
	/* ignore */
    } else if (xmlStrEqual(name, BAD_CAST "osm")) {
        printStatus();
        filetype = FILETYPE_NONE;
    } else if (xmlStrEqual(name, BAD_CAST "osmChange")) {
        printStatus();
        filetype = FILETYPE_NONE;
    } else if (xmlStrEqual(name, BAD_CAST "planetdiff")) {
        printStatus();
        filetype = FILETYPE_NONE;
    } else if (xmlStrEqual(name, BAD_CAST "bound")) {
        /* ignore */
    } else if (xmlStrEqual(name, BAD_CAST "bounds")) {
        /* ignore */
    } else if (xmlStrEqual(name, BAD_CAST "changeset")) {
        /* ignore */
    tags.resetList(); /* We may have accumulated some tags even if we ignored the changeset */
    } else if (xmlStrEqual(name, BAD_CAST "add")) {
        action = ACTION_NONE;
    } else if (xmlStrEqual(name, BAD_CAST "create")) {
        action = ACTION_NONE;
    } else if (xmlStrEqual(name, BAD_CAST "modify")) {
        action = ACTION_NONE;
    } else if (xmlStrEqual(name, BAD_CAST "delete")) {
        action = ACTION_NONE;
    } else {
        fprintf(stderr, "%s: Unknown element name: %s\n", __FUNCTION__, name);
    }
}

void parse_xml2_t::processNode(xmlTextReaderPtr reader, struct osmdata_t *osmdata) {
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

parse_xml2_t::parse_xml2_t(const int extra_attributes_, const bool bbox_, const boost::shared_ptr<reprojection>& projection_,
		const double minlon, const double minlat, const double maxlon, const double maxlat):
		parse_t(extra_attributes_, bbox_, projection_, minlon, minlat, maxlon, maxlat)
{
    LIBXML_TEST_VERSION;
}

parse_xml2_t::~parse_xml2_t()
{
    xmlCleanupParser();
    xmlMemoryDump();
}

int parse_xml2_t::streamFile(const char *filename, const int sanitize, osmdata_t *osmdata) {
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
