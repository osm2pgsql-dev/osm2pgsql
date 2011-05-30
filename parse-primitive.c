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

#include "osmtypes.h"
#include "sanitizer.h"
#include "reprojection.h"
#include "input.h"
#include "output.h"


char *extractAttribute(char **token, int tokens, char *attname)
{
    char buffer[256];
    int cl;
    int i;
    sprintf(buffer, "%s=\"", attname);
    cl = strlen(buffer);
    for (i=0; i<tokens; i++)
    {
        if (!strncmp(token[i], buffer, cl))
        {
            char *quote = index(token[i] + cl, '"');
            if (quote == NULL) quote = token[i] + strlen(token[i]) + 1;
            *quote = 0;
            if (strchr(token[i]+cl, '&') == 0) return (token[i] + cl);

            char *in;
            char *out;
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
static actions_t ParseAction(char **token, int tokens, struct osmdata_t *osmdata)
{
    if( osmdata->filetype == FILETYPE_OSMCHANGE || osmdata->filetype == FILETYPE_PLANETDIFF )
        return osmdata->action;
    actions_t new_action = ACTION_NONE;
    char *action = extractAttribute(token, tokens, "action");
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

static void StartElement(char *name, char *line, struct osmdata_t *osmdata)
{
    char *xid, *xlat, *xlon, *xk, *xv, *xrole, *xtype;
    char *token[255];
    int tokens = 0;

    if (osmdata->filetype == FILETYPE_NONE)
    {
        if (!strcmp(name, "?xml")) return;
        if (!strcmp(name, "osm"))
        {
            osmdata->filetype = FILETYPE_OSM;
            osmdata->action = ACTION_CREATE;
        }
        else if (!strcmp(name, "osmChange"))
        {
            osmdata->filetype = FILETYPE_OSMCHANGE;
            osmdata->action = ACTION_NONE;
        }
        else if (!strcmp(name, "planetdiff"))
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

    tokens=1;
    token[0] = line;
    int quote = 0;
    char *i;
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
        assert(xid);
        osmdata->osm_id = strtol((char *)xid, NULL, 10);
        osmdata->action = ParseAction(token, tokens, osmdata);

        if (osmdata->action != ACTION_DELETE) {
            xlon = extractAttribute(token, tokens, "lon");
            xlat = extractAttribute(token, tokens, "lat");
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

            addItem(&(osmdata->tags), xk, (char *)xv, 0);
        }
    } else if (!strcmp(name, "way")) {

        xid  = extractAttribute(token, tokens, "id");
        assert(xid);
        osmdata->osm_id   = strtol((char *)xid, NULL, 10);
        osmdata->action = ParseAction(token, tokens, osmdata);

        if (osmdata->osm_id > osmdata->max_way)
            osmdata->max_way = osmdata->osm_id;

        osmdata->count_way++;
        if (osmdata->count_way%1000 == 0)
            printStatus(osmdata);

        osmdata->nd_count = 0;
    } else if (!strcmp(name, "nd")) {
        xid  = extractAttribute(token, tokens, "ref");
        assert(xid);

        osmdata->nds[osmdata->nd_count++] = strtol( (char *)xid, NULL, 10 );

        if( osmdata->nd_count >= osmdata->nd_max )
          realloc_nodes(osmdata);
    } else if (!strcmp(name, "relation")) {
        xid  = extractAttribute(token, tokens, "id");
        assert(xid);
        osmdata->osm_id   = strtol((char *)xid, NULL, 10);
        osmdata->action = ParseAction(token, tokens, osmdata);

        if (osmdata->osm_id > osmdata->max_rel)
            osmdata->max_rel = osmdata->osm_id;

        osmdata->count_rel++;
        if (osmdata->count_rel%10 == 0)
            printStatus(osmdata);

        osmdata->member_count = 0;
    } else if (!strcmp(name, "member")) {
        xrole = extractAttribute(token, tokens, "role");
        assert(xrole);

        xtype = extractAttribute(token, tokens, "type");
        assert(xtype);

        xid  = extractAttribute(token, tokens, "ref");
        assert(xid);

        osmdata->members[osmdata->member_count].id   = strtol( (char *)xid, NULL, 0 );
        osmdata->members[osmdata->member_count].role = strdup( (char *)xrole );

        /* Currently we are only interested in 'way' members since these form polygons with holes */
        if (!strcmp(xtype, "way"))
            osmdata->members[osmdata->member_count].type = OSMTYPE_WAY;
        else if (!strcmp(xtype, "node"))
            osmdata->members[osmdata->member_count].type = OSMTYPE_NODE;
        else if (!strcmp(xtype, "relation"))
            osmdata->members[osmdata->member_count].type = OSMTYPE_RELATION;
        osmdata->member_count++;

        if( osmdata->member_count >= osmdata->member_max )
            realloc_members(osmdata);
    } else if (!strcmp(name, "add") ||
               !strcmp(name, "create")) {
        osmdata->action = ACTION_MODIFY; // Turns all creates into modifies, makes it resiliant against inconsistant snapshots.
    } else if (!strcmp(name, "modify")) {
        osmdata->action = ACTION_MODIFY;
    } else if (!strcmp(name, "delete")) {
        osmdata->action = ACTION_DELETE;
    } else if (!strcmp(name, "bound")) {
        /* ignore */
    } else if (!strcmp(name, "bounds")) {
        /* ignore */
    } else if (!strcmp(name, "changeset")) {
        /* ignore */
    } else {
        fprintf(stderr, "%s: Unknown element name: %s\n", __FUNCTION__, name);
    }

    // Collect extra attribute information and add as tags
    if (osmdata->extra_attributes && (!strcmp(name, "node") ||
				      !strcmp(name, "way") ||
				      !strcmp(name, "relation")))
    {
        char *xtmp;

        xtmp = extractAttribute(token, tokens, "user");
        if (xtmp) {
	  addItem(&(osmdata->tags), "osm_user", (char *)xtmp, 0);
        }

        xtmp = extractAttribute(token, tokens, "uid");
        if (xtmp) {
	  addItem(&(osmdata->tags), "osm_uid", (char *)xtmp, 0);
        }

        xtmp = extractAttribute(token, tokens, "version");
        if (xtmp) {
	  addItem(&(osmdata->tags), "osm_version", (char *)xtmp, 0);
        }

        xtmp = extractAttribute(token, tokens, "timestamp");
        if (xtmp) {
	  addItem(&(osmdata->tags), "osm_timestamp", (char *)xtmp, 0);
        }
    }
}

static void EndElement(const char *name, struct osmdata_t *osmdata)
{
    if (!strcmp(name, "node")) {
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
    } else if (!strcmp(name, "way")) {
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
    } else if (!strcmp(name, "relation")) {
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
    } else if (!strcmp(name, "tag")) {
        /* ignore */
    } else if (!strcmp(name, "nd")) {
        /* ignore */
    } else if (!strcmp(name, "member")) {
	/* ignore */
    } else if (!strcmp(name, "osm")) {
        printStatus(osmdata);
        osmdata->filetype = FILETYPE_NONE;
    } else if (!strcmp(name, "osmChange")) {
        printStatus(osmdata);
        osmdata->filetype = FILETYPE_NONE;
    } else if (!strcmp(name, "planetdiff")) {
        printStatus(osmdata);
        osmdata->filetype = FILETYPE_NONE;
    } else if (!strcmp(name, "bound")) {
        /* ignore */
    } else if (!strcmp(name, "bounds")) {
        /* ignore */
    } else if (!strcmp(name, "changeset")) {
        /* ignore */
      resetList(&(osmdata->tags)); /* We may have accumulated some tags even if we ignored the changeset */
    } else if (!strcmp(name, "add")) {
        osmdata->action = ACTION_NONE;
    } else if (!strcmp(name, "create")) {
        osmdata->action = ACTION_NONE;
    } else if (!strcmp(name, "modify")) {
        osmdata->action = ACTION_NONE;
    } else if (!strcmp(name, "delete")) {
        osmdata->action = ACTION_NONE;
    } else {
        fprintf(stderr, "%s: Unknown element name: %s\n", __FUNCTION__, name);
    }
}

static void process(char *line, struct osmdata_t *osmdata) {
    char *lt = index(line, '<');
    if (lt)
    {
        char *spc = index(lt+1, ' ');
        char *gt = index(lt+1, '>');
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
            //printf ("nx=%d, lt+1=#%s#\n", nx-lt,lt+1);
            StartElement(lt+1, nx, osmdata);
            if (slash) EndElement(lt+1, osmdata);
        }
    }
}

int streamFilePrimitive(char *filename, int sanitize UNUSED, struct osmdata_t *osmdata) {
    struct Input *i;
    char buffer[65536];
    int bufsz = 0;
    int offset = 0;

    i = inputOpen(filename);

    if (i != NULL) {
        while(1)
        {
            bufsz = bufsz + readFile(i, buffer + bufsz, sizeof(buffer) - bufsz);
            char *nl = index(buffer, '\n');
            if (nl == 0) break;
            *nl=0;
            while (nl && nl < buffer + bufsz)
            {
                *nl = 0;
                process(buffer + offset, osmdata);
                offset = nl - buffer + 1;
                //printf("\nsearch line at %d, buffer sz is %d = ",offset, bufsz);
                nl = index(buffer + offset, '\n');
                //printf("%d\n", nl ? nl-buffer : -1);
            }
            memcpy(buffer, buffer + offset, bufsz - offset);
            bufsz = bufsz - offset;
            offset = 0;
        }
    } else {
        fprintf(stderr, "Unable to open %s\n", filename);
        return 1;
    }
    return 0;
}
