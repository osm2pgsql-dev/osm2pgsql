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
#include "parse.hpp"

#ifdef BUILD_READER_PBF
#  include "parse-pbf.hpp"
#endif

int verbose;

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
	  return new output_pgsql_t();
	} else if (strcmp("gazetteer", output_backend) == 0) {
	  return new output_gazetteer_t();
	} else if (strcmp("null", output_backend) == 0) {
	  return new output_null_t();
	} else {
	  fprintf(stderr, "Output backend `%s' not recognised. Should be one of [pgsql, gazetteer, null].\n", output_backend);
	  exit(EXIT_FAILURE);
	}
}

 
int main(int argc, char *argv[])
{
	output_options options;

    int create=0;
    int sanitize=0;
    int long_usage_bool=0;
    int pass_prompt=0;
    const char *db = "gis";
    const char *username=NULL;
    const char *host=NULL;
    const char *password=NULL;
    const char *port = "5432";
    const char *temparg;
    const char *output_backend = "pgsql";
    const char *input_reader = "auto";
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
            case 'a': options.append=1;   break;
            case 'b': bbox=optarg; break;
            case 'c': create=1;   break;
            case 'v': verbose=1;  break;
            case 's': options.slim=1;     break;
            case 'K': options.keep_coastlines=1;     break;
            case 'u': sanitize=1; break;
            case 'l': options.projection.reset(new reprojection(PROJ_LATLONG));  break;
            case 'm': options.projection.reset(new reprojection(PROJ_SPHERE_MERC)); break;
            case 'M': options.projection.reset(new reprojection(PROJ_MERC)); break;
            case 'E': options.projection.reset(new reprojection(-atoi(optarg))); break;
            case 'p': options.prefix=optarg; break;
            case 'd': db=optarg;  break;
            case 'C': options.cache = atoi(optarg); break;
            case 'U': username=optarg; break;
            case 'W': pass_prompt=1; break;
            case 'H': host=optarg; break;
            case 'P': port=optarg; break;
            case 'S': options.style=optarg; break;
            case 'i': options.tblsmain_index=options.tblsslim_index=optarg; break;
            case 200: options.tblsslim_data=optarg; break;
            case 201: options.tblsslim_index=optarg; break;
            case 202: options.tblsmain_data=optarg; break;
            case 203: options.tblsmain_index=optarg; break;
            case 'e':
            	options.expire_tiles_zoom_min = atoi(optarg);
                temparg = strchr(optarg, '-');
                if (temparg) options.expire_tiles_zoom = atoi(temparg + 1);
                if (options.expire_tiles_zoom < options.expire_tiles_zoom_min) options.expire_tiles_zoom = options.expire_tiles_zoom_min;
                break;
            case 'o': options.expire_tiles_filename=optarg; break;
            case 'O': output_backend = optarg; break;
            case 'x': extra_attributes=1; break;
            case 'k':  if (options.enable_hstore != HSTORE_NONE) { fprintf(stderr, "ERROR: You can not specify both --hstore (-k) and --hstore-all (-j)\n"); exit (EXIT_FAILURE); }
            options.enable_hstore=HSTORE_NORM; break;
            case 208: options.hstore_match_only = 1; break;
            case 'j': if (options.enable_hstore != HSTORE_NONE) { fprintf(stderr, "ERROR: You can not specify both --hstore (-k) and --hstore-all (-j)\n"); exit (EXIT_FAILURE); }
            options.enable_hstore=HSTORE_ALL; break;
            case 'z': 
            	options.n_hstore_columns++;
            	options.hstore_columns = (const char**)realloc(options.hstore_columns, sizeof(char *) * options.n_hstore_columns);
            	options.hstore_columns[options.n_hstore_columns-1] = optarg;
                break;
            case 'G': options.enable_multi=1; break;
            case 'r': input_reader = optarg; break;
            case 'h': long_usage_bool=1; break;
            case 'I': 
#ifdef HAVE_PTHREAD
            	options.parallel_indexing=0;
#endif
                break;
            case 204:
                if (strcmp(optarg,"dense") == 0) options.alloc_chunkwise = ALLOC_DENSE;
                else if (strcmp(optarg,"chunk") == 0) options.alloc_chunkwise = ALLOC_DENSE | ALLOC_DENSE_CHUNK;
                else if (strcmp(optarg,"sparse") == 0) options.alloc_chunkwise = ALLOC_SPARSE;
                else if (strcmp(optarg,"optimized") == 0) options.alloc_chunkwise = ALLOC_DENSE | ALLOC_SPARSE;
                else {fprintf(stderr, "ERROR: Unrecognized cache strategy %s.\n", optarg); exit(EXIT_FAILURE); }
                break;
            case 205:
#ifdef HAVE_FORK                
            	options.num_procs = atoi(optarg);
#else
                fprintf(stderr, "WARNING: osm2pgsql was compiled without fork, only using one process!\n");
#endif
                break;
            case 206: options.droptemp = 1; break;
            case 207: options.unlogged = 1; break;
            case 209:
            	options.flat_node_cache_enabled = 1;
            	options.flat_node_file = optarg;
            	break;
            case 210: options.excludepoly = 1; break;
            case 211: options.enable_hstore_index = 1; break;
            case 212: options.tag_transform_script = optarg; break;
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

    if (options.append && create) {
        fprintf(stderr, "Error: --append and --create options can not be used at the same time!\n");
        exit(EXIT_FAILURE);
    }

    if (options.droptemp && !options.slim) {
        fprintf(stderr, "Error: --drop only makes sense with --slim.\n");
        exit(EXIT_FAILURE);
    }

    if (options.unlogged && !create) {
        fprintf(stderr, "Warning: --unlogged only makes sense with --create; ignored.\n");
        options.unlogged = 0;
    }

    if (options.enable_hstore == HSTORE_NONE && !options.n_hstore_columns && options.hstore_match_only)
    {
        fprintf(stderr, "Warning: --hstore-match-only only makes sense with --hstore, --hstore-all, or --hstore-column; ignored.\n");
        options.hstore_match_only = 0;
    }

    if (options.enable_hstore_index && options.enable_hstore == HSTORE_NONE && !options.n_hstore_columns) {
        fprintf(stderr, "Warning: --hstore-add-index only makes sense with hstore enabled.\n");
        options.enable_hstore_index = 0;
    }

    if (options.cache < 0) options.cache = 0;

    if (options.cache == 0) {
        fprintf(stderr, "WARNING: ram cache is disabled. This will likely slow down processing a lot.\n\n");
    }
    if (sizeof(int*) == 4 && options.slim != 1) {
        fprintf(stderr, "\n!! You are running this on 32bit system, so at most\n");
        fprintf(stderr, "!! 3GB of RAM can be used. If you encounter unexpected\n");
        fprintf(stderr, "!! exceptions during import, you should try running in slim\n");
        fprintf(stderr, "!! mode using parameter -s.\n");
    }

    if (pass_prompt)
        password = simple_prompt("Password:", 100, 0);
    else {
        password = getenv("PGPASS");
    }

    if (options.num_procs < 1) options.num_procs = 1;

    
    // Check the database
    options.conninfo = build_conninfo(db, username, password, host, port);
    PGconn *sql_conn = PQconnectdb(options.conninfo);
    if (PQstatus(sql_conn) != CONNECTION_OK) {
        fprintf(stderr, "Error: Connection to database failed: %s\n", PQerrorMessage(sql_conn));
        exit(EXIT_FAILURE);
    }
    if (options.unlogged && PQserverVersion(sql_conn) < 90100) {
        fprintf(stderr, "Error: --unlogged works only with PostgreSQL 9.1 and above, but\n");
        fprintf(stderr, "you are using PostgreSQL %d.%d.%d.\n", PQserverVersion(sql_conn) / 10000, (PQserverVersion(sql_conn) / 100) % 100, PQserverVersion(sql_conn) % 100);
        exit(EXIT_FAILURE);
    }
    PQfinish(sql_conn);

    text_init();

    LIBXML_TEST_VERSION;

    //setup the backend (output)
    output_t* out = get_output(output_backend);
    osmdata_t osmdata(out);

    //setup the middle
    middle_t* mid = options.slim ? ((middle_t *)new middle_pgsql_t()) : ((middle_t *)new middle_ram_t());

    //setup the front (input)
    parse_delegate_t parser(extra_attributes, bbox, options.projection);

    fprintf(stderr, "Using projection SRS %d (%s)\n", 
    		options.projection->project_getprojinfo()->srs,
    		options.projection->project_getprojinfo()->descr );

    options.scale = (options.projection->get_proj_id() == PROJ_LATLONG) ? 10000000 : 100;
    options.mid = mid;
    options.out = out;

    //start it up
    time_t overall_start = time(NULL);
    out->start(&options, parser.getProjection());

    //read in the input files one by one
    while (optind < argc) {
        //read the actual input
        fprintf(stderr, "\nReading in file: %s\n", argv[optind]);
        time_t start = time(NULL);
        if (parser.streamFile(input_reader, argv[optind], sanitize, &osmdata) != 0)
            exit_nicely();
        fprintf(stderr, "  parse time: %ds\n", (int)(time(NULL) - start));
        optind++;
    }

    xmlCleanupParser();
    xmlMemoryDump();
    
    //show stats
    parser.printSummary();

    /* done with output_*_t */
    out->stop();
    out->cleanup();
    delete out;

    /* done with middle_*_t */
    delete mid;

    /* free the column pointer buffer */
    free(options.hstore_columns);

    text_exit();
    fprintf(stderr, "\nOsm2pgsql took %ds overall\n", (int)(time(NULL) - overall_start));

    return 0;
}
