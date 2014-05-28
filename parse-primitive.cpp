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

/*

This is a version of osm2pgsql without proper XML parsing
it should arrive at the same results as the normal osm2pgsql
and take an hour less to process the full planet file but
YMMV. This is just a proof of concept and should not be used
in a production environment.

 */

#define _GNU_SOURCE
#define UNUSED  __attribute__ ((unused))

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>


#include "sanitizer.hpp"
#include "input.hpp"
#include "parse-primitive.hpp"
#include "output.hpp"
#include "util.hpp"


char *extractAttribute(char **token, int tokens, const char *attname)
{
    char buffer[256];
    int cl;
    int i;
    char *in;
    char *out;
    sprintf(buffer, "%s=\"", attname);
    cl = strlen(buffer);
    for (i=0; i<tokens; i++)
    {
        if (!strncmp(token[i], buffer, cl))
        {
            char *quote = strchr(token[i] + cl, '"');
            if (quote == NULL) quote = token[i] + strlen(token[i]) + 1;
            *quote = 0;
            if (strchr(token[i]+cl, '&') == 0) return (token[i] + cl);

            for (in=token[i]+cl, out=token[i]+cl; *in; in++)
            {
                if (*in == '&')
                {
                    if (!strncmp(in+1, "quot;", 5))
                    {
                        *out++ = '"';
                        in+=5;
                    }
                    else if (!strncmp(in+1, "lt;", 3))
                    {
                        *out++ = '<';
                        in+=3;
                    }
                    else if (!strncmp(in+1, "gt;", 3))
                    {
                        *out++ = '>';
                        in+=3;
                    }
                    else if (!strncmp(in+1, "apos;", 5))
                    {
                        *out++ = '\'';
                        in+=5;
                    }
                }
                else
                {
                    *out++ = *in;
                }
            }
            *out = 0;
            return (token[i]+cl);
        }
    }
    return NULL;
}

/* Parses the action="foo" tags in JOSM change files. Obvisouly not useful from osmChange files */
actions_t parse_primitive_t::ParseAction(char **token, int tokens)
{
    actions_t new_action;
    char *action_text;
    if( filetype == FILETYPE_OSMCHANGE || filetype == FILETYPE_PLANETDIFF )
        return action;
    new_action = ACTION_NONE;
    action_text = extractAttribute(token, tokens, "action");
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

void parse_primitive_t::StartElement(char *name, char *line, struct osmdata_t *osmdata)
{
    char *xid, *xlat, *xlon, *xk, *xv, *xrole, *xtype;
    char *token[255];
    int tokens = 0;
    int quote = 0;
    char *i;

    if (filetype == FILETYPE_NONE)
    {
        if (!strcmp(name, "?xml")) return;
        if (!strcmp(name, "osm"))
        {
            filetype = FILETYPE_OSM;
            action = ACTION_CREATE;
        }
        else if (!strcmp(name, "osmChange"))
        {
            filetype = FILETYPE_OSMCHANGE;
            action = ACTION_NONE;
        }
        else if (!strcmp(name, "planetdiff"))
        {
            filetype = FILETYPE_PLANETDIFF;
            action = ACTION_NONE;
        }
        else
        {
            fprintf( stderr, "Unknown XML document type: %s\n", name );
            util::exit_nicely();
        }
        return;
    }

    tokens=1;
    token[0] = line;
    for (i=line; *i; i++)
    {
        if (quote)
        {
            if (*i == '"') 
            {
                quote = 0;
            }
        }
        else
        {
            if (*i == '"')
            {
                quote = 1;
            }
            else if (isspace(*i))
            {
                *i = 0;
                token[tokens++] = i + 1;
            }
        }
    }

    if (!strcmp(name, "node")) {
        xid  = extractAttribute(token, tokens, "id");
        xlon = extractAttribute(token, tokens, "lon");
        xlat = extractAttribute(token, tokens, "lat");
        assert(xid); assert(xlon); assert(xlat);

        osm_id  = strtoosmid((char *)xid, NULL, 10);
        node_lon = strtod((char *)xlon, NULL);
        node_lat = strtod((char *)xlat, NULL);
        action = ParseAction(token, tokens);

        if (osm_id > max_node)
            max_node = osm_id;

        if (count_node == 0) {
            time(&start_node);
        }
        
        count_node++;
        if (count_node%10000 == 0)
            printStatus();

    } else if (!strcmp(name, "tag")) {
        xk = extractAttribute(token, tokens, "k");
        assert(xk);

        /* 'created_by' and 'source' are common and not interesting to mapnik renderer */
        if (strcmp((char *)xk, "created_by") && strcmp((char *)xk, "source")) {
            char *p;
            xv = extractAttribute(token, tokens, "v");
            assert(xv);
            while ((p = strchr(xk, ' ')))
                *p = '_';

            addItem(&(tags), xk, (char *)xv, 0);
        }
    } else if (!strcmp(name, "way")) {

        xid  = extractAttribute(token, tokens, "id");
        assert(xid);
        osm_id   = strtoosmid((char *)xid, NULL, 10);
        action = ParseAction(token, tokens);

        if (osm_id > max_way)
            max_way = osm_id;
        
        if (count_way == 0) {
            time(&start_way);
        }
        
        count_way++;
        if (count_way%1000 == 0)
            printStatus();

        nd_count = 0;
    } else if (!strcmp(name, "nd")) {
        xid  = extractAttribute(token, tokens, "ref");
        assert(xid);

        nds[nd_count++] = strtoosmid( (char *)xid, NULL, 10 );

        if( nd_count >= nd_max )
          realloc_nodes();
    } else if (!strcmp(name, "relation")) {
        xid  = extractAttribute(token, tokens, "id");
        assert(xid);
        osm_id   = strtoosmid((char *)xid, NULL, 10);
        action = ParseAction(token, tokens);

        if (osm_id > max_rel)
            max_rel = osm_id;
        
        if (count_rel == 0) {
            time(&start_rel);
        }

        count_rel++;
        if (count_rel%10 == 0)
            printStatus();

        member_count = 0;
    } else if (!strcmp(name, "member")) {
        xrole = extractAttribute(token, tokens, "role");
        assert(xrole);

        xtype = extractAttribute(token, tokens, "type");
        assert(xtype);

        xid  = extractAttribute(token, tokens, "ref");
        assert(xid);

        members[member_count].id   = strtoosmid( (char *)xid, NULL, 0 );
        members[member_count].role = strdup( (char *)xrole );

        /* Currently we are only interested in 'way' members since these form polygons with holes */
        if (!strcmp(xtype, "way"))
            members[member_count].type = OSMTYPE_WAY;
        else if (!strcmp(xtype, "node"))
            members[member_count].type = OSMTYPE_NODE;
        else if (!strcmp(xtype, "relation"))
            members[member_count].type = OSMTYPE_RELATION;
        member_count++;

        if( member_count >= member_max )
        	realloc_members();
    } else if (!strcmp(name, "add") ||
               !strcmp(name, "create")) {
        action = ACTION_MODIFY; /* Turns all creates into modifies, makes it resiliant against inconsistant snapshots. */
    } else if (!strcmp(name, "modify")) {
        action = ACTION_MODIFY;
    } else if (!strcmp(name, "delete")) {
        action = ACTION_DELETE;
    } else if (!strcmp(name, "bound")) {
        /* ignore */
    } else if (!strcmp(name, "bounds")) {
        /* ignore */
    } else if (!strcmp(name, "changeset")) {
        /* ignore */
    } else {
        fprintf(stderr, "%s: Unknown element name: %s\n", __FUNCTION__, name);
    }

    /* Collect extra attribute information and add as tags */
    if (extra_attributes && (!strcmp(name, "node") ||
				      !strcmp(name, "way") ||
				      !strcmp(name, "relation")))
    {
        char *xtmp;

        xtmp = extractAttribute(token, tokens, "user");
        if (xtmp) {
	  addItem(&(tags), "osm_user", (char *)xtmp, 0);
        }

        xtmp = extractAttribute(token, tokens, "uid");
        if (xtmp) {
	  addItem(&(tags), "osm_uid", (char *)xtmp, 0);
        }

        xtmp = extractAttribute(token, tokens, "version");
        if (xtmp) {
	  addItem(&(tags), "osm_version", (char *)xtmp, 0);
        }

        xtmp = extractAttribute(token, tokens, "timestamp");
        if (xtmp) {
	  addItem(&(tags), "osm_timestamp", (char *)xtmp, 0);
        }
    }
}

void parse_primitive_t::EndElement(const char *name, struct osmdata_t *osmdata)
{
    if (!strcmp(name, "node")) {
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
      resetList(&(tags));
    } else if (!strcmp(name, "way")) {
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
      resetList(&(tags));
    } else if (!strcmp(name, "relation")) {
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
        resetList(&(tags));
        resetMembers();
    } else if (!strcmp(name, "tag")) {
        /* ignore */
    } else if (!strcmp(name, "nd")) {
        /* ignore */
    } else if (!strcmp(name, "member")) {
	/* ignore */
    } else if (!strcmp(name, "osm")) {
        printStatus();
        filetype = FILETYPE_NONE;
    } else if (!strcmp(name, "osmChange")) {
        printStatus();
        filetype = FILETYPE_NONE;
    } else if (!strcmp(name, "planetdiff")) {
        printStatus();
        filetype = FILETYPE_NONE;
    } else if (!strcmp(name, "bound")) {
        /* ignore */
    } else if (!strcmp(name, "bounds")) {
        /* ignore */
    } else if (!strcmp(name, "changeset")) {
        /* ignore */
      resetList(&(tags)); /* We may have accumulated some tags even if we ignored the changeset */
    } else if (!strcmp(name, "add")) {
        action = ACTION_NONE;
    } else if (!strcmp(name, "create")) {
        action = ACTION_NONE;
    } else if (!strcmp(name, "modify")) {
        action = ACTION_NONE;
    } else if (!strcmp(name, "delete")) {
        action = ACTION_NONE;
    } else {
        fprintf(stderr, "%s: Unknown element name: %s\n", __FUNCTION__, name);
    }
}

void parse_primitive_t::process(char *line, struct osmdata_t *osmdata) {
    char *lt = strchr(line, '<');
    if (lt)
    {
        char *spc = strchr(lt+1, ' ');
        char *gt = strchr(lt+1, '>');
        char *nx = spc;
        if (*(lt+1) == '/')
        {
            *gt = 0;
            EndElement(lt+2, osmdata);
        }
        else
        {
            int slash = 0;
            if (gt != NULL) { 
                *gt-- = 0; 
                if (nx == NULL || gt < nx) nx = gt; 
                while(gt>lt)
                {
                    if (*gt=='/') { slash=1; *gt=0; break; }
                    if (!isspace(*gt)) break;
                    gt--;
                }
            }
            *nx++ = 0;
            /* printf ("nx=%d, lt+1=#%s#\n", nx-lt,lt+1); */
            StartElement(lt+1, nx, osmdata);
            if (slash) EndElement(lt+1, osmdata);
        }
    }
}

parse_primitive_t::parse_primitive_t(const int extra_attributes_, const bool bbox_, const boost::shared_ptr<reprojection>& projection_,
		const double minlon, const double minlat, const double maxlon, const double maxlat, keyval& tags):
		parse_t(extra_attributes_, bbox_, projection_, minlon, minlat, maxlon, maxlat, tags)
{

}

parse_primitive_t::~parse_primitive_t()
{

}

int parse_primitive_t::streamFile(const char *filename, const int UNUSED, osmdata_t *osmdata) {
    struct Input *i;
    char buffer[65536];
    int bufsz = 0;
    int offset = 0;
    char *nl;

    i = inputOpen(filename);

    if (i != NULL) {
        while(1)
        {
            bufsz = bufsz + readFile(i, buffer + bufsz, sizeof(buffer) - bufsz - 1);
            buffer[bufsz] = 0;
            nl = strchr(buffer, '\n');
            if (nl == 0) break;
            *nl=0;
            while (nl && nl < buffer + bufsz)
            {
                *nl = 0;
                process(buffer + offset, osmdata);
                offset = nl - buffer + 1;
                nl = strchr(buffer + offset, '\n');
            }
            memcpy(buffer, buffer + offset, bufsz - offset);
            bufsz = bufsz - offset;
            offset = 0;
        }
    } else {
        fprintf(stderr, "Unable to open %s\n", filename);
        return 1;
    }
    inputClose(i);
    return 0;
}
