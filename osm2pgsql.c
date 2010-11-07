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

#include "config.h"

#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <getopt.h>
#include <libgen.h>
#include <time.h>

#include <libpq-fe.h>

#include <libxml/xmlstring.h>
#include <libxml/xmlreader.h>

#include "osmtypes.h"
#include "build_geometry.h"
#include "middle-pgsql.h"
#include "middle-ram.h"
#include "output-pgsql.h"
#include "output-gazetteer.h"
#include "output-null.h"
#include "sanitizer.h"
#include "reprojection.h"
#include "text-tree.h"
#include "input.h"
#include "sprompt.h"
#include "parse-xml2.h"
#include "parse-primitive.h"

#ifdef BUILD_READER_PBF
#  include "parse-pbf.h"
#endif

#define INIT_MAX_MEMBERS 64
#define INIT_MAX_NODES  4096

int verbose;

// Data structure carrying all parsing related variables
static struct osmdata_t osmdata = { 
  .filetype = FILETYPE_NONE,
  .action   = ACTION_NONE,
  .bbox     = NULL
};


static int parse_bbox(struct osmdata_t *osmdata)
{
    int n;

    if (!osmdata->bbox)
        return 0;

    n = sscanf(osmdata->bbox, "%lf,%lf,%lf,%lf", &(osmdata->minlon), &(osmdata->minlat), &(osmdata->maxlon), &(osmdata->maxlat));
    if (n != 4) {
        fprintf(stderr, "Bounding box must be specified like: minlon,minlat,maxlon,maxlat\n");
        return 1;
    }
    if (osmdata->maxlon <= osmdata->minlon) {
        fprintf(stderr, "Bounding box failed due to maxlon <= minlon\n");
        return 1;
    }
    if (osmdata->maxlat <= osmdata->minlat) {
        fprintf(stderr, "Bounding box failed due to maxlat <= minlat\n");
        return 1;
    }
    printf("Applying Bounding box: %f,%f to %f,%f\n", osmdata->minlon, osmdata->minlat, osmdata->maxlon, osmdata->maxlat);
    return 0;
}



void exit_nicely()
{
    fprintf(stderr, "Error occurred, cleaning up\n");
    osmdata.out->cleanup();
    exit(1);
}
 
static void short_usage(char *arg0)
{
    const char *name = basename(arg0);

    fprintf(stderr, "Usage error. For further information see:\n");
    fprintf(stderr, "\t%s -h|--help\n", name);
}

static void long_usage(char *arg0)
{
    int i;
    const char *name = basename(arg0);

    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "\t%s [options] planet.osm\n", name);
    fprintf(stderr, "\t%s [options] planet.osm.{gz,bz2}\n", name);
    fprintf(stderr, "\t%s [options] file1.osm file2.osm file3.osm\n", name);
    fprintf(stderr, "\nThis will import the data from the OSM file(s) into a PostgreSQL database\n");
    fprintf(stderr, "suitable for use by the Mapnik renderer\n");
    fprintf(stderr, "\nOptions:\n");
    fprintf(stderr, "   -a|--append\t\tAdd the OSM file into the database without removing\n");
    fprintf(stderr, "              \t\texisting data.\n");
    fprintf(stderr, "   -b|--bbox\t\tApply a bounding box filter on the imported data\n");
    fprintf(stderr, "              \t\tMust be specified as: minlon,minlat,maxlon,maxlat\n");
    fprintf(stderr, "              \t\te.g. --bbox -0.5,51.25,0.5,51.75\n");
    fprintf(stderr, "   -c|--create\t\tRemove existing data from the database. This is the \n");
    fprintf(stderr, "              \t\tdefault if --append is not specified.\n");
    fprintf(stderr, "   -d|--database\tThe name of the PostgreSQL database to connect\n");
    fprintf(stderr, "                \tto (default: gis).\n");
    fprintf(stderr, "   -i|--tablespace-index\tThe name of the PostgreSQL tablespace where indexes will be create\n");
    fprintf(stderr, "                \tto (default: pg_default).\n");
    fprintf(stderr, "   -l|--latlong\t\tStore data in degrees of latitude & longitude.\n");
    fprintf(stderr, "   -m|--merc\t\tStore data in proper spherical mercator (default)\n");
    fprintf(stderr, "   -M|--oldmerc\t\tStore data in the legacy OSM mercator format\n");
    fprintf(stderr, "   -E|--proj num\tUse projection EPSG:num\n");
    fprintf(stderr, "   -u|--utf8-sanitize\tRepair bad UTF8 input data (present in planet\n");
    fprintf(stderr, "                \tdumps prior to August 2007). Adds about 10%% overhead.\n");
    fprintf(stderr, "   -p|--prefix\t\tPrefix for table names (default planet_osm)\n");
    fprintf(stderr, "   -s|--slim\t\tStore temporary data in the database. This greatly\n");
    fprintf(stderr, "            \t\treduces the RAM usage but is much slower.\n");

    if (sizeof(int*) == 4) {
        fprintf(stderr, "            \t\tYou are running this on 32bit system, so at most\n");
        fprintf(stderr, "            \t\t3GB of RAM will be used. If you encounter unexpected\n");
        fprintf(stderr, "            \t\texceptions during import, you should try this switch.\n");
    }
    
    fprintf(stderr, "   -S|--style\t\tLocation of the style file. Defaults to " OSM2PGSQL_DATADIR "/default.style\n");
    fprintf(stderr, "   -C|--cache\t\tOnly for slim mode: Use upto this many MB for caching nodes\n");
    fprintf(stderr, "             \t\tDefault is 800\n");
    fprintf(stderr, "   -U|--username\tPostgresql user name.\n");
    fprintf(stderr, "   -W|--password\tForce password prompt.\n");
    fprintf(stderr, "   -H|--host\t\tDatabase server hostname or socket location.\n");
    fprintf(stderr, "   -P|--port\t\tDatabase server port.\n");
    fprintf(stderr, "   -e|--expire-tiles [min_zoom-]max_zoom\tCreate a tile expiry list.\n");
    fprintf(stderr, "   -o|--expire-output filename\tOutput filename for expired tiles list.\n");
    fprintf(stderr, "   -r|--input-reader\tInput frontend.\n");
    fprintf(stderr, "              \t\tlibxml2   - Parse XML using libxml2. (default)\n");
    fprintf(stderr, "              \t\tprimitive - Primitive XML parsing.\n");
#ifdef BUILD_READER_PBF
    fprintf(stderr, "              \t\tpbf       - OSM binary format.\n");
#endif
    fprintf(stderr, "   -O|--output\t\tOutput backend.\n");
    fprintf(stderr, "              \t\tpgsql - Output to a PostGIS database. (default)\n");
    fprintf(stderr, "              \t\tgazetteer - Output to a PostGIS database suitable for gazetteer\n");
    fprintf(stderr, "              \t\tnull  - No output. Useful for testing.\n");
    fprintf(stderr, "   -x|--extra-attributes\n");
    fprintf(stderr, "              \t\tInclude attributes for each object in the database.\n");
    fprintf(stderr, "              \t\tThis includes the username, userid, timestamp and version.\n"); 
    fprintf(stderr, "              \t\tNote: this option also requires additional entries in your style file.\n"); 
    fprintf(stderr, "   -k|--hstore\t\tGenerate an additional hstore (key/value) column to  postgresql tables\n");
    fprintf(stderr, "   -z|--hstore-column\tGenerate an additional hstore (key/value) column to containing all tags\n");
    fprintf(stderr, "                     \tthat start with the specified string, eg --hstore-column \"name:\" will\n");
    fprintf(stderr, "                     \tproduce an extra hstore column that contains all name:xx tags\n");
    fprintf(stderr, "   -G|--multi-geometry\t\tGenerate multi-geometry features in postgresql tables.\n");
    fprintf(stderr, "   -h|--help\t\tHelp information.\n");
    fprintf(stderr, "   -v|--verbose\t\tVerbose output.\n");
    fprintf(stderr, "\n");
    if(!verbose)
    {
        fprintf(stderr, "Add -v to display supported projections.\n");
        fprintf(stderr, "Use -E to access any espg projections (usually in /usr/share/proj/epsg)\n" );
    }
    else
    {
        fprintf(stderr, "Supported projections:\n" );
        for(i=0; i<PROJ_COUNT; i++ )
        {
            fprintf( stderr, "%-20s(%2s) SRS:%6d %s\n", 
                    Projection_Info[i].descr, Projection_Info[i].option, Projection_Info[i].srs, Projection_Info[i].proj4text);
        }
    }
}

const char *build_conninfo(const char *db, const char *username, const char *password, const char *host, const char *port)
{
    static char conninfo[1024];

    conninfo[0]='\0';
    strcat(conninfo, "dbname='");
    strcat(conninfo, db);
    strcat(conninfo, "'");

    if (username) {
        strcat(conninfo, " user='");
        strcat(conninfo, username);
        strcat(conninfo, "'");
    }
    if (password) {
        strcat(conninfo, " password='");
        strcat(conninfo, password);
        strcat(conninfo, "'");
    }
    if (host) {
        strcat(conninfo, " host='");
        strcat(conninfo, host);
        strcat(conninfo, "'");
    }
    if (port) {
        strcat(conninfo, " port='");
        strcat(conninfo, port);
        strcat(conninfo, "'");
    }

    return conninfo;
}

void realloc_nodes(struct osmdata_t *osmdata)
{
  if( osmdata->nd_max == 0 )
    osmdata->nd_max = INIT_MAX_NODES;
  else
    osmdata->nd_max <<= 1;
    
  osmdata->nds = realloc( osmdata->nds, osmdata->nd_max * sizeof( osmdata->nds[0] ) );
  if( !osmdata->nds )
  {
    fprintf( stderr, "Failed to expand node list to %d\n", osmdata->nd_max );
    exit_nicely();
  }
}

void realloc_members(struct osmdata_t *osmdata)
{
  if( osmdata->member_max == 0 )
    osmdata->member_max = INIT_MAX_NODES;
  else
    osmdata->member_max <<= 1;
    
  osmdata->members = realloc( osmdata->members, osmdata->member_max * sizeof( osmdata->members[0] ) );
  if( !osmdata->members )
  {
    fprintf( stderr, "Failed to expand member list to %d\n", osmdata->member_max );
    exit_nicely();
  }
}

void resetMembers(struct osmdata_t *osmdata)
{
  for(unsigned i = 0; i < osmdata->member_count; i++ )
    free( osmdata->members[i].role );
}

void printStatus(struct osmdata_t *osmdata)
{
    fprintf(stderr, "\rProcessing: Node(%dk) Way(%dk) Relation(%dk)",
            osmdata->count_node/1000, osmdata->count_way/1000, osmdata->count_rel/1000);
}

int node_wanted(struct osmdata_t *osmdata, double lat, double lon)
{
    if (!osmdata->bbox)
        return 1;

    if (lat < osmdata->minlat || lat > osmdata->maxlat)
        return 0;
    if (lon < osmdata->minlon || lon > osmdata->maxlon)
        return 0;
    return 1;
}

int main(int argc, char *argv[])
{
    int append=0;
    int create=0;
    int slim=0;
    int sanitize=0;
    int long_usage_bool=0;
    int pass_prompt=0;
    int projection = PROJ_SPHERE_MERC;
    int expire_tiles_zoom = -1;
    int expire_tiles_zoom_min = -1;
    int enable_hstore = 0;
    int enable_multi = 0;
    const char *expire_tiles_filename = "dirty_tiles";
    const char *db = "gis";
    const char *username=NULL;
    const char *host=NULL;
    const char *password=NULL;
    const char *port = "5432";
    const char *tblsindex = "pg_default"; // default TABLESPACE for index
    const char *conninfo = NULL;
    const char *prefix = "planet_osm";
    const char *style = OSM2PGSQL_DATADIR "/default.style";
    const char *temparg;
    const char *output_backend = "pgsql";
    const char *input_reader = "auto";
    const char **hstore_columns = NULL;
    int n_hstore_columns = 0;
    int cache = 800;
    struct output_options options;
    PGconn *sql_conn;
    
    int (*streamFile)(char *, int, struct osmdata_t *);

    fprintf(stderr, "osm2pgsql SVN version %s\n\n", VERSION);

    while (1) {
        int c, option_index = 0;
        static struct option long_options[] = {
            {"append",   0, 0, 'a'},
            {"bbox",     1, 0, 'b'},
            {"create",   0, 0, 'c'},
            {"database", 1, 0, 'd'},
            {"latlong",  0, 0, 'l'},
            {"verbose",  0, 0, 'v'},
            {"slim",     0, 0, 's'},
            {"prefix",   1, 0, 'p'},
            {"proj",     1, 0, 'E'},
            {"merc",     0, 0, 'm'},
            {"oldmerc",  0, 0, 'M'},
            {"utf8-sanitize", 0, 0, 'u'},
            {"cache",    1, 0, 'C'},
            {"username", 1, 0, 'U'},
            {"password", 0, 0, 'W'},
            {"host",     1, 0, 'H'},
            {"port",     1, 0, 'P'},
            {"tablespace-index", 1, 0, 'i'},
            {"help",     0, 0, 'h'},
            {"style",    1, 0, 'S'},
            {"expire-tiles", 1, 0, 'e'},
            {"expire-output", 1, 0, 'o'},
            {"output",   1, 0, 'O'},
            {"extra-attributes", 0, 0, 'x'},
            {"hstore", 0, 0, 'k'},
            {"hstore-column", 1, 0, 'z'},
            {"multi-geometry", 0, 0, 'G'},
	    {"input-reader", 1, 0, 'r'},
            {"version", 0, 0, 'V'},
            {0, 0, 0, 0}
        };

        c = getopt_long (argc, argv, "ab:cd:hlmMp:suvU:WH:P:i:E:C:S:e:o:O:xkGz:r:V", long_options, &option_index);
        if (c == -1)
            break;

        switch (c) {
            case 'a': append=1;   break;
            case 'b': osmdata.bbox=optarg; break;
            case 'c': create=1;   break;
            case 'v': verbose=1;  break;
            case 's': slim=1;     break;
            case 'u': sanitize=1; break;
            case 'l': projection=PROJ_LATLONG;  break;
            case 'm': projection=PROJ_SPHERE_MERC; break;
            case 'M': projection=PROJ_MERC; break;
            case 'E': projection=-atoi(optarg); break;
            case 'p': prefix=optarg; break;
            case 'd': db=optarg;  break;
            case 'C': cache = atoi(optarg); break;
            case 'U': username=optarg; break;
            case 'W': pass_prompt=1; break;
            case 'H': host=optarg; break;
            case 'P': port=optarg; break;
            case 'S': style=optarg; break;
            case 'i': tblsindex=optarg; break;
            case 'e':
                expire_tiles_zoom_min = atoi(optarg);
		temparg = strchr(optarg, '-');
		if (temparg) expire_tiles_zoom = atoi(temparg + 1);
		if (expire_tiles_zoom < expire_tiles_zoom_min) expire_tiles_zoom = expire_tiles_zoom_min;
                break;
            case 'o': expire_tiles_filename=optarg; break;
	    case 'O': output_backend = optarg; break;
            case 'x': osmdata.extra_attributes=1; break;
	    case 'k': enable_hstore=1; break;
            case 'z': 
                n_hstore_columns++;
                hstore_columns = (const char**)realloc(hstore_columns, sizeof(&n_hstore_columns) * n_hstore_columns);
                hstore_columns[n_hstore_columns-1] = optarg;
                break;
	    case 'G': enable_multi=1; break;
	    case 'r': input_reader = optarg; break;
            case 'h': long_usage_bool=1; break;
  	    case 'V': exit(EXIT_SUCCESS);
            case '?':
            default:
                short_usage(argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    if (long_usage_bool) {
        long_usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    if (argc == optind) {  // No non-switch arguments
        short_usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    if (append && create) {
        fprintf(stderr, "Error: --append and --create options can not be used at the same time!\n");
        exit(EXIT_FAILURE);
    }

    if( cache < 0 ) cache = 0;

    if (pass_prompt)
        password = simple_prompt("Password:", 100, 0);
    else {
        password = getenv("PGPASS");
    }	
        

    conninfo = build_conninfo(db, username, password, host, port);
    sql_conn = PQconnectdb(conninfo);
    if (PQstatus(sql_conn) != CONNECTION_OK) {
        fprintf(stderr, "Connection to database failed: %s\n", PQerrorMessage(sql_conn));
        exit(EXIT_FAILURE);
    }
    PQfinish(sql_conn);

    text_init();
    initList(&osmdata.tags);

    osmdata.count_node = osmdata.max_node = 0;
    osmdata.count_way  = osmdata.max_way  = 0;
    osmdata.count_rel  = osmdata.max_rel  = 0;

    LIBXML_TEST_VERSION

    project_init(projection);
    fprintf(stderr, "Using projection SRS %d (%s)\n", 
        project_getprojinfo()->srs, project_getprojinfo()->descr );

    if (parse_bbox(&osmdata))
        return 1;

    options.conninfo = conninfo;
    options.prefix = prefix;
    options.append = append;
    options.slim = slim;
    options.projection = project_getprojinfo()->srs;
    options.scale = (projection==PROJ_LATLONG)?10000000:100;
    options.mid = slim ? &mid_pgsql : &mid_ram;
    options.cache = cache;
    options.style = style;
    options.tblsindex = tblsindex;
    options.expire_tiles_zoom = expire_tiles_zoom;
    options.expire_tiles_zoom_min = expire_tiles_zoom_min;
    options.expire_tiles_filename = expire_tiles_filename;
    options.enable_multi = enable_multi;
    options.enable_hstore = enable_hstore;
    options.hstore_columns = hstore_columns;
    options.n_hstore_columns = n_hstore_columns;

    if (strcmp("pgsql", output_backend) == 0) {
      osmdata.out = &out_pgsql;
    } else if (strcmp("gazetteer", output_backend) == 0) {
      osmdata.out = &out_gazetteer;
    } else if (strcmp("null", output_backend) == 0) {
      osmdata.out = &out_null;
    } else {
      fprintf(stderr, "Output backend `%s' not recognised. Should be one of [pgsql, gazetteer, null].\n", output_backend);
      exit(EXIT_FAILURE);
    }

    if (strcmp("auto", input_reader) != 0) {
      if (strcmp("libxml2", input_reader) == 0) {
        streamFile = &streamFileXML2;
      } else if (strcmp("primitive", input_reader) == 0) {
        streamFile = &streamFilePrimitive;
#ifdef BUILD_READER_PBF
      } else if (strcmp("pbf", input_reader) == 0) {
        streamFile = &streamFilePbf;
#endif
      } else {
        fprintf(stderr, "Input parser `%s' not recognised. Should be one of [libxml2, primitive"
#ifdef BUILD_READER_PBF
	      ", pbf"
#endif
	      "].\n", input_reader);
      exit(EXIT_FAILURE);
      }
    }

    osmdata.out->start(&options);

    realloc_nodes(&osmdata);
    realloc_members(&osmdata);

    if (sizeof(int*) == 4 && options.slim != 1) {
        fprintf(stderr, "\n!! You are running this on 32bit system, so at most\n");
        fprintf(stderr, "!! 3GB of RAM can be used. If you encounter unexpected\n");
        fprintf(stderr, "!! exceptions during import, you should try running in slim\n");
        fprintf(stderr, "!! mode using parameter -s.\n");
    }

    while (optind < argc) {
        /* if input_reader is not forced by -r switch try to auto-detect it
           by file extension */
        if (strcmp("auto", input_reader) == 0) {
#ifdef BUILD_READER_PBF
          if (strcasecmp(".pbf",argv[optind]+strlen(argv[optind])-4) == 0) {
            streamFile = &streamFilePbf;
          } else {
            streamFile = &streamFileXML2;
          }
#else
          streamFile = &streamFileXML2;
#endif
        }
        time_t start, end;

        fprintf(stderr, "\nReading in file: %s\n", argv[optind]);
        time(&start);
        if (streamFile(argv[optind], sanitize, &osmdata) != 0)
            exit_nicely();
        time(&end);
        fprintf(stderr, "  parse time: %ds\n", (int)(end - start));
        optind++;
    }

    xmlCleanupParser();
    xmlMemoryDump();
    
    if (osmdata.count_node || osmdata.count_way || osmdata.count_rel) {
        fprintf(stderr, "\n");
        fprintf(stderr, "Node stats: total(%d), max(%d)\n", osmdata.count_node, osmdata.max_node);
        fprintf(stderr, "Way stats: total(%d), max(%d)\n", osmdata.count_way, osmdata.max_way);
        fprintf(stderr, "Relation stats: total(%d), max(%d)\n", osmdata.count_rel, osmdata.max_rel);
    }
    osmdata.out->stop();
    
    free(osmdata.nds);
    free(osmdata.members);
    
    // free the column pointer buffer
    free(hstore_columns);

    project_exit();
    text_exit();
    fprintf(stderr, "\n");

    return 0;
}
