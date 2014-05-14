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

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <getopt.h>
#include <libgen.h>
#include <time.h>
#include <stdexcept>

#include <libpq-fe.h>

#include <libxml/xmlstring.h>
#include <libxml/xmlreader.h>

#include "osmtypes.hpp"
#include "build_geometry.hpp"
#include "middle-pgsql.hpp"
#include "middle-ram.hpp"
#include "node-ram-cache.hpp"
#include "output-pgsql.hpp"
#include "output-gazetteer.hpp"
#include "output-null.hpp"
#include "sanitizer.hpp"
#include "reprojection.hpp"
#include "text-tree.hpp"
#include "input.hpp"
#include "sprompt.hpp"
#include "parse-xml2.hpp"
#include "parse-primitive.hpp"
#include "parse-o5m.hpp"

#ifdef BUILD_READER_PBF
#  include "parse-pbf.hpp"
#endif

int verbose;

//TODO: move this typedef to a shared parsing header
typedef int (*stream_input_file_t)(const char *, int, struct osmdata_t *);

static void short_usage(char *arg0)
{
    const char *name = basename(arg0);

    fprintf(stderr, "Usage error. For further information see:\n");
    fprintf(stderr, "\t%s -h|--help\n", name);
}

static void long_usage(char *arg0)
{
    const char *name = basename(arg0);

    printf("Usage:\n");
    printf("\t%s [options] planet.osm\n", name);
    printf("\t%s [options] planet.osm.{pbf,gz,bz2}\n", name);
    printf("\t%s [options] file1.osm file2.osm file3.osm\n", name);
    printf("\nThis will import the data from the OSM file(s) into a PostgreSQL database\n");
    printf("suitable for use by the Mapnik renderer.\n\n");

    printf("%s", "\
Common options:\n\
   -a|--append      Add the OSM file into the database without removing\n\
                    existing data.\n\
   -c|--create      Remove existing data from the database. This is the\n\
                    default if --append is not specified.\n\
   -l|--latlong     Store data in degrees of latitude & longitude.\n\
   -m|--merc        Store data in proper spherical mercator (default).\n\
   -E|--proj num    Use projection EPSG:num.\n\
   -s|--slim        Store temporary data in the database. This greatly\n\
                    reduces the RAM usage but is much slower. This switch is\n\
                    required if you want to update with --append later.\n\
   -S|--style       Location of the style file. Defaults to\n");
    printf("                    %s/default.style.\n", OSM2PGSQL_DATADIR);
    printf("%s", "\
   -C|--cache       Use up to this many MB for caching nodes (default: 800)\n\
\n\
Database options:\n\
   -d|--database    The name of the PostgreSQL database to connect\n\
                    to (default: gis).\n\
   -U|--username    PostgreSQL user name (specify passsword in PGPASS\n\
                    environment variable or use -W).\n\
   -W|--password    Force password prompt.\n\
   -H|--host        Database server host name or socket location.\n\
   -P|--port        Database server port.\n");

    if (verbose) 
    {
        printf("%s", "\
Hstore options:\n\
   -k|--hstore      Add tags without column to an additional hstore\n\
                    (key/value) column\n\
      --hstore-match-only   Only keep objects that have a value in one of\n\
                    the columns (default with --hstore is to keep all objects)\n\
   -j|--hstore-all  Add all tags to an additional hstore (key/value) column\n\
   -z|--hstore-column   Add an additional hstore (key/value) column containing\n\
                    all tags that start with the specified string, eg\n\
                    --hstore-column \"name:\" will produce an extra hstore\n\
                    column that contains all name:xx tags\n\
      --hstore-add-index    Add index to hstore column.\n\
\n\
Obsolete options:\n\
   -u|--utf8-sanitize   Repair bad UTF8 input data (present in planet\n\
                    dumps prior to August 2007). Adds about 10% overhead.\n\
   -M|--oldmerc     Store data in the legacy OSM mercator format\n\
\n\
Performance options:\n\
   -i|--tablespace-index    The name of the PostgreSQL tablespace where\n\
                    all indexes will be created.\n\
                    The following options allow more fine-grained control:\n\
      --tablespace-main-data    tablespace for main tables\n\
      --tablespace-main-index   tablespace for main table indexes\n\
      --tablespace-slim-data    tablespace for slim mode tables\n\
      --tablespace-slim-index   tablespace for slim mode indexes\n\
                    (if unset, use db's default; -i is equivalent to setting\n\
                    --tablespace-main-index and --tablespace-slim-index)\n\
      --drop        only with --slim: drop temporary tables after import \n\
                    (no updates are possible).\n\
      --number-processes        Specifies the number of parallel processes \n\
                    used for certain operations (default is 1).\n\
   -I|--disable-parallel-indexing   Disable indexing all tables concurrently.\n\
      --unlogged    Use unlogged tables (lost on crash but faster). \n\
                    Requires PostgreSQL 9.1.\n\
      --cache-strategy  Specifies the method used to cache nodes in ram.\n\
                    Available options are:\n\
                    dense: caching strategy optimised for full planet import\n\
                    chunk: caching strategy optimised for non-contiguous \n\
                        memory allocation\n\
                    sparse: caching strategy optimised for small extracts\n\
                    optimized: automatically combines dense and sparse \n\
                        strategies for optimal storage efficiency. This may\n\
                        us twice as much virtual memory, but no more physical \n\
                        memory.\n");
#ifdef __amd64__
    printf("                    The default is \"optimized\"\n");
#else
    /* use "chunked" as a default in 32 bit compilations, as it is less wasteful of virtual memory than "optimized"*/
    printf("                    The default is \"sparse\"\n");
#endif
    printf("%s", "\
      --flat-nodes  Specifies the flat file to use to persistently store node \n\
                    information in slim mode instead of in PostgreSQL.\n\
                    This file is a single > 16Gb large file. Only recommended\n\
                    for full planet imports. Default is disabled.\n\
\n\
Expiry options:\n\
   -e|--expire-tiles [min_zoom-]max_zoom    Create a tile expiry list.\n\
   -o|--expire-output filename  Output filename for expired tiles list.\n\
\n\
Other options:\n\
   -b|--bbox        Apply a bounding box filter on the imported data\n\
                    Must be specified as: minlon,minlat,maxlon,maxlat\n\
                    e.g. --bbox -0.5,51.25,0.5,51.75\n\
   -p|--prefix      Prefix for table names (default planet_osm)\n\
   -r|--input-reader    Input frontend.\n\
                    libxml2   - Parse XML using libxml2. (default)\n\
                    primitive - Primitive XML parsing.\n");
#ifdef BUILD_READER_PBF
    printf("                    pbf       - OSM binary format.\n");
#endif
    printf("\
   -O|--output      Output backend.\n\
                    pgsql - Output to a PostGIS database. (default)\n\
                    gazetteer - Output to a PostGIS database for Nominatim\n\
                    null - No output. Useful for testing.\n");
#ifdef HAVE_LUA
    printf("\
      --tag-transform-script  Specify a lua script to handle tag filtering and normalisation\n\
                    The script contains callback functions for nodes, ways and relations, which each\n\
                    take a set of tags and returns a transformed, filtered set of tags which are then\n\
                    written to the database.\n");
#endif
    printf("\
   -x|--extra-attributes\n\
                    Include attributes for each object in the database.\n\
                    This includes the username, userid, timestamp and version.\n\
                    Requires additional entries in your style file.\n\
   -G|--multi-geometry  Generate multi-geometry features in postgresql tables.\n\
   -K|--keep-coastlines Keep coastline data rather than filtering it out.\n\
                    By default natural=coastline tagged data will be discarded\n\
                    because renderers usually have shape files for them.\n\
      --exclude-invalid-polygon   do not import polygons with invalid geometries.\n\
   -h|--help        Help information.\n\
   -v|--verbose     Verbose output.\n");
    }
    else
    {
        printf("\n");
        printf("A typical command to import a full planet is\n");
        printf("    %s -c -d gis --slim -C <cache size> -k \\\n", name);
        printf("      --flat-nodes <flat nodes> planet-latest.osm.pbf\n");
        printf("where\n");
        printf("    <cache size> is 20000 on machines with 24GB or more RAM \n");
        printf("      or about 75%% of memory in MB on machines with less\n");
        printf("    <flat nodes> is a location where a 19GB file can be saved.\n");
        printf("\n");
        printf("A typical command to update a database imported with the above command is\n");
        printf("    osmosis --rri workingDirectory=<osmosis dir> --simc --wx - \\\n");
        printf("      | %s -a -d gis --slim -k --flat-nodes <flat nodes> \n", name);
        printf("where\n");
        printf("    <flat nodes> is the same location as above.\n");
        printf("    <osmosis dir> is the location osmosis replication was initialized to.\n");
        printf("\nRun %s --help --verbose (-h -v) for a full list of options.\n", name);
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

void exit_nicely()
{
    fprintf(stderr, "Error occurred, cleaning up\n");
    exit(1);
}

output_t* get_output(const char* output_backend)
{
	if (strcmp("pgsql", output_backend) == 0) {
	  return &out_pgsql;
	} else if (strcmp("gazetteer", output_backend) == 0) {
	  return &out_gazetteer;
	} else if (strcmp("null", output_backend) == 0) {
	  return &out_null;
	} else {
	  fprintf(stderr, "Output backend `%s' not recognised. Should be one of [pgsql, gazetteer, null].\n", output_backend);
	  exit(EXIT_FAILURE);
	}
}

stream_input_file_t get_input_reader(const char* input_reader, const char* filename)
{
	// if input_reader is forced to a specific iput format
	if (strcmp("auto", input_reader) != 0) {
		if (strcmp("libxml2", input_reader) == 0) {
			return &streamFileXML2;
		} else if (strcmp("primitive", input_reader) == 0) {
			return &streamFilePrimitive;
#ifdef BUILD_READER_PBF
		} else if (strcmp("pbf", input_reader) == 0) {
			return &streamFilePbf;
#endif
		} else if (strcmp("o5m", input_reader) == 0) {
			return &streamFileO5m;
		} else {
			fprintf(stderr, "Input parser `%s' not recognised. Should be one of [libxml2, primitive, o5m"
#ifdef BUILD_READER_PBF
							", pbf"
#endif
							"].\n", input_reader);
			exit(EXIT_FAILURE);
		}
	} // if input_reader is not forced by -r switch try to auto-detect it by file extension
	else {
		if (strcasecmp(".pbf", filename + strlen(filename) - 4) == 0) {
#ifdef BUILD_READER_PBF
			return &streamFilePbf;
#else
			fprintf(stderr, "ERROR: PBF support has not been compiled into this version of osm2pgsql, please either compile it with pbf support or use one of the other input formats\n");
			exit(EXIT_FAILURE);
#endif
		} else if (strcasecmp(".o5m", filename + strlen(filename) - 4) == 0
				|| strcasecmp(".o5c", filename + strlen(filename) - 4) == 0) {
			return &streamFileO5m;
		} else {
			return &streamFileXML2;
		}
	}
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
    int enable_hstore = HSTORE_NONE;
    int enable_hstore_index = 0;
    int hstore_match_only = 0;
    int enable_multi = 0;
    int parallel_indexing = 1;
    int flat_node_cache_enabled = 0;
#ifdef __amd64__
    int alloc_chunkwise = ALLOC_SPARSE | ALLOC_DENSE;
#else
    int alloc_chunkwise = ALLOC_SPARSE;
#endif
    int num_procs = 1;
    int droptemp = 0;
    int unlogged = 0;
    int excludepoly = 0;
    time_t start, end;
    time_t overall_start, overall_end;
    time_t now;
    time_t end_nodes;
    time_t end_way;
    time_t end_rel;
    const char *expire_tiles_filename = "dirty_tiles";
    const char *db = "gis";
    const char *username=NULL;
    const char *host=NULL;
    const char *password=NULL;
    const char *port = "5432";
    const char *tblsmain_index = NULL; /* no default TABLESPACE for index on main tables */
    const char *tblsmain_data = NULL;  /* no default TABLESPACE for main tables */
    const char *tblsslim_index = NULL; /* no default TABLESPACE for index on slim mode tables */
    const char *tblsslim_data = NULL;  /* no default TABLESPACE for slim mode tables */
    const char *conninfo = NULL;
    const char *prefix = "planet_osm";
    const char *style = OSM2PGSQL_DATADIR "/default.style";
    const char *temparg;
    const char *output_backend = "pgsql";
    const char *input_reader = "auto";
    const char **hstore_columns = NULL;
    const char *flat_nodes_file = NULL;
    const char *tag_transform_script = NULL;
    int n_hstore_columns = 0;
    int keep_coastlines=0;
    int cache = 800;
    struct output_options options;
    PGconn *sql_conn;
    const char *bbox=NULL;
    int extra_attributes=0;
    

    fprintf(stderr, "osm2pgsql SVN version %s (%lubit id space)\n\n", VERSION, 8 * sizeof(osmid_t));

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
            {"tablespace-slim-data", 1, 0, 200},
            {"tablespace-slim-index", 1, 0, 201},
            {"tablespace-main-data", 1, 0, 202},
            {"tablespace-main-index", 1, 0, 203},
            {"help",     0, 0, 'h'},
            {"style",    1, 0, 'S'},
            {"expire-tiles", 1, 0, 'e'},
            {"expire-output", 1, 0, 'o'},
            {"output",   1, 0, 'O'},
            {"extra-attributes", 0, 0, 'x'},
            {"hstore", 0, 0, 'k'},
            {"hstore-all", 0, 0, 'j'},
            {"hstore-column", 1, 0, 'z'},
            {"hstore-match-only", 0, 0, 208},
            {"hstore-add-index",0,0,211},
            {"multi-geometry", 0, 0, 'G'},
            {"keep-coastlines", 0, 0, 'K'},
            {"input-reader", 1, 0, 'r'},
            {"version", 0, 0, 'V'},
            {"disable-parallel-indexing", 0, 0, 'I'},
            {"cache-strategy", 1, 0, 204},
            {"number-processes", 1, 0, 205},
            {"drop", 0, 0, 206},
            {"unlogged", 0, 0, 207},
            {"flat-nodes",1,0,209},
            {"exclude-invalid-polygon",0,0,210},
            {"tag-transform-script",1,0,212},
            {0, 0, 0, 0}
        };

        c = getopt_long (argc, argv, "ab:cd:KhlmMp:suvU:WH:P:i:IE:C:S:e:o:O:xkjGz:r:V", long_options, &option_index);
        if (c == -1)
            break;

        switch (c) {
            case 'a': append=1;   break;
            case 'b': bbox=optarg; break;
            case 'c': create=1;   break;
            case 'v': verbose=1;  break;
            case 's': slim=1;     break;
            case 'K': keep_coastlines=1;     break;
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
            case 'i': tblsmain_index=tblsslim_index=optarg; break;
            case 200: tblsslim_data=optarg; break;    
            case 201: tblsslim_index=optarg; break;    
            case 202: tblsmain_data=optarg; break;    
            case 203: tblsmain_index=optarg; break;    
            case 'e':
                expire_tiles_zoom_min = atoi(optarg);
                temparg = strchr(optarg, '-');
                if (temparg) expire_tiles_zoom = atoi(temparg + 1);
                if (expire_tiles_zoom < expire_tiles_zoom_min) expire_tiles_zoom = expire_tiles_zoom_min;
                break;
            case 'o': expire_tiles_filename=optarg; break;
            case 'O': output_backend = optarg; break;
            case 'x': extra_attributes=1; break;
            case 'k':  if (enable_hstore != HSTORE_NONE) { fprintf(stderr, "ERROR: You can not specify both --hstore (-k) and --hstore-all (-j)\n"); exit (EXIT_FAILURE); }
                enable_hstore=HSTORE_NORM; break;
            case 208: hstore_match_only = 1; break;
            case 'j': if (enable_hstore != HSTORE_NONE) { fprintf(stderr, "ERROR: You can not specify both --hstore (-k) and --hstore-all (-j)\n"); exit (EXIT_FAILURE); }
                enable_hstore=HSTORE_ALL; break;
            case 'z': 
                n_hstore_columns++;
                hstore_columns = (const char**)realloc(hstore_columns, sizeof(char *) * n_hstore_columns);
                hstore_columns[n_hstore_columns-1] = optarg;
                break;
            case 'G': enable_multi=1; break;
            case 'r': input_reader = optarg; break;
            case 'h': long_usage_bool=1; break;
            case 'I': 
#ifdef HAVE_PTHREAD
                parallel_indexing=0; 
#endif
                break;
            case 204:
                if (strcmp(optarg,"dense") == 0) alloc_chunkwise = ALLOC_DENSE;
                else if (strcmp(optarg,"chunk") == 0) alloc_chunkwise = ALLOC_DENSE | ALLOC_DENSE_CHUNK;
                else if (strcmp(optarg,"sparse") == 0) alloc_chunkwise = ALLOC_SPARSE;
                else if (strcmp(optarg,"optimized") == 0) alloc_chunkwise = ALLOC_DENSE | ALLOC_SPARSE;
                else {fprintf(stderr, "ERROR: Unrecognized cache strategy %s.\n", optarg); exit(EXIT_FAILURE); }
                break;
            case 205:
#ifdef HAVE_FORK                
                num_procs = atoi(optarg);
#else
                fprintf(stderr, "WARNING: osm2pgsql was compiled without fork, only using one process!\n");
#endif
                break;
            case 206: droptemp = 1; break;
            case 207: unlogged = 1; break;
            case 209:
            	flat_node_cache_enabled = 1;
            	flat_nodes_file = optarg;
            	break;
            case 210: excludepoly = 1; exclude_broken_polygon(); break;
            case 211: enable_hstore_index = 1; break;
            case 212: tag_transform_script = optarg; break;
            case 'V': exit(EXIT_SUCCESS);
            case '?':
            default:
                short_usage(argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    if (long_usage_bool) {
        long_usage(argv[0]);
        exit(EXIT_SUCCESS);
    }

    if (argc == optind) {  /* No non-switch arguments */
        short_usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    if (append && create) {
        fprintf(stderr, "Error: --append and --create options can not be used at the same time!\n");
        exit(EXIT_FAILURE);
    }

    if (droptemp && !slim) {
        fprintf(stderr, "Error: --drop only makes sense with --slim.\n");
        exit(EXIT_FAILURE);
    }

    if (unlogged && !create) {
        fprintf(stderr, "Warning: --unlogged only makes sense with --create; ignored.\n");
        unlogged = 0;
    }

    if (enable_hstore == HSTORE_NONE && !n_hstore_columns && hstore_match_only)
    {
        fprintf(stderr, "Warning: --hstore-match-only only makes sense with --hstore, --hstore-all, or --hstore-column; ignored.\n");
        hstore_match_only = 0;
    }

    if (enable_hstore_index && enable_hstore  == HSTORE_NONE && !n_hstore_columns) {
        fprintf(stderr, "Warning: --hstore-add-index only makes sense with hstore enabled.\n");
        enable_hstore_index = 0;
    }

    if (cache < 0) cache = 0;

    if (cache == 0) {
        fprintf(stderr, "WARNING: ram cache is disabled. This will likely slow down processing a lot.\n\n");
    }

    if (num_procs < 1) num_procs = 1;

    if (pass_prompt)
        password = simple_prompt("Password:", 100, 0);
    else {
        password = getenv("PGPASS");
    }

    
    // Check the database
    conninfo = build_conninfo(db, username, password, host, port);
    sql_conn = PQconnectdb(conninfo);
    if (PQstatus(sql_conn) != CONNECTION_OK) {
        fprintf(stderr, "Error: Connection to database failed: %s\n", PQerrorMessage(sql_conn));
        exit(EXIT_FAILURE);
    }
    if (unlogged && PQserverVersion(sql_conn) < 90100) {
        fprintf(stderr, "Error: --unlogged works only with PostgreSQL 9.1 and above, but\n");
        fprintf(stderr, "you are using PostgreSQL %d.%d.%d.\n", PQserverVersion(sql_conn) / 10000, (PQserverVersion(sql_conn) / 100) % 100, PQserverVersion(sql_conn) % 100);
        exit(EXIT_FAILURE);
    }
    PQfinish(sql_conn);

    text_init();

    LIBXML_TEST_VERSION

    project_init(projection);
    fprintf(stderr, "Using projection SRS %d (%s)\n", 
        project_getprojinfo()->srs, project_getprojinfo()->descr );

    options.conninfo = conninfo;
    options.prefix = prefix;
    options.append = append;
    options.slim = slim;
    options.projection = project_getprojinfo()->srs;
    options.scale = (projection==PROJ_LATLONG)?10000000:100;
    options.mid = slim ? ((middle_t *)&mid_pgsql) : ((middle_t *)&mid_ram);
    options.cache = cache;
    options.style = style;
    options.tblsmain_index = tblsmain_index;
    options.tblsmain_data = tblsmain_data;
    options.tblsslim_index = tblsslim_index;
    options.tblsslim_data = tblsslim_data;
    options.expire_tiles_zoom = expire_tiles_zoom;
    options.expire_tiles_zoom_min = expire_tiles_zoom_min;
    options.expire_tiles_filename = expire_tiles_filename;
    options.enable_multi = enable_multi;
    options.enable_hstore = enable_hstore;
    options.enable_hstore_index = enable_hstore_index;
    options.hstore_match_only = hstore_match_only;
    options.hstore_columns = hstore_columns;
    options.n_hstore_columns = n_hstore_columns;
    options.keep_coastlines = keep_coastlines;
    options.parallel_indexing = parallel_indexing;
    options.alloc_chunkwise = alloc_chunkwise;
    options.num_procs = num_procs;
    options.droptemp = droptemp;
    options.unlogged = unlogged;
    options.flat_node_cache_enabled = flat_node_cache_enabled;
    options.flat_node_file = flat_nodes_file;
    options.excludepoly = excludepoly;
    options.tag_transform_script = tag_transform_script;
    options.out = get_output(output_backend);

    //setup the output
    osmdata_t osmdata;
    try
    {
    	osmdata.init(options.out, extra_attributes, bbox);
    }
    catch(std::exception& e)
    {
    	fprintf(stderr, "%s", e.what());
    	return 1;
    }

    //start it up
    time(&overall_start);
    osmdata.out->start(&options);
    osmdata.realloc_nodes();
    osmdata.realloc_members();

    if (sizeof(int*) == 4 && options.slim != 1) {
        fprintf(stderr, "\n!! You are running this on 32bit system, so at most\n");
        fprintf(stderr, "!! 3GB of RAM can be used. If you encounter unexpected\n");
        fprintf(stderr, "!! exceptions during import, you should try running in slim\n");
        fprintf(stderr, "!! mode using parameter -s.\n");
    }

    //read in the input files one by one
    while (optind < argc) {
        //figure how we are going to read the input
        stream_input_file_t streamFile = get_input_reader(input_reader, argv[optind]);

        //read the actual input
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
        time(&now);
        end_nodes = osmdata.start_way > 0 ? osmdata.start_way : now;
        end_way = osmdata.start_rel > 0 ? osmdata.start_rel : now;
        end_rel =  now;
        fprintf(stderr, "\n");
        fprintf(stderr, "Node stats: total(%" PRIdOSMID "), max(%" PRIdOSMID ") in %is\n", osmdata.count_node, osmdata.max_node,
                osmdata.count_node > 0 ? (int)(end_nodes - osmdata.start_node) : 0);
        fprintf(stderr, "Way stats: total(%" PRIdOSMID "), max(%" PRIdOSMID ") in %is\n", osmdata.count_way, osmdata.max_way,
                osmdata.count_way > 0 ? (int)(end_way - osmdata.start_way) : 0);
        fprintf(stderr, "Relation stats: total(%" PRIdOSMID "), max(%" PRIdOSMID ") in %is\n", osmdata.count_rel, osmdata.max_rel,
                osmdata.count_rel > 0 ? (int)(end_rel - osmdata.start_rel) : 0);
    }
    osmdata.out->stop();

    /* free the column pointer buffer */
    free(hstore_columns);

    project_exit();
    text_exit();
    fprintf(stderr, "\n");
    time(&overall_end);
    fprintf(stderr, "Osm2pgsql took %ds overall\n", (int)(overall_end - overall_start));

    return 0;
}
