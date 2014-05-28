#include "options.hpp"
#include "sprompt.hpp"

#include "parse.hpp"
#include "middle.hpp"
#include "output.hpp"

#include "middle-pgsql.hpp"
#include "middle-ram.hpp"
#include "output-pgsql.hpp"
#include "output-gazetteer.hpp"
#include "output-null.hpp"

#include <getopt.h>
#include <libgen.h>
#include <stdio.h>
#include <string.h>
#include <stdexcept>
#include <boost/format.hpp>

namespace
{
    const char * short_options = "ab:cd:KhlmMp:suvU:WH:P:i:IE:C:S:e:o:O:xkjGz:r:V";
    const struct option long_options[] =
    {
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

    void short_usage(char *arg0)
    {
        throw std::runtime_error((boost::format("Usage error. For further information see:\n\t%1% -h|--help\n") % basename(arg0)).str());
    }

    void long_usage(char *arg0, bool verbose = false)
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
}


options_t::options_t():
    conninfo(NULL), prefix("planet_osm"), scale(DEFAULT_SCALE), projection(new reprojection(PROJ_SPHERE_MERC)), append(0), slim(0),
    cache(800), tblsmain_index(NULL), tblsslim_index(NULL), tblsmain_data(NULL), tblsslim_data(NULL), style(OSM2PGSQL_DATADIR "/default.style"),
    expire_tiles_zoom(-1), expire_tiles_zoom_min(-1), expire_tiles_filename("dirty_tiles"), enable_hstore(HSTORE_NONE), enable_hstore_index(0),
    enable_multi(0), hstore_columns(), keep_coastlines(0), parallel_indexing(1),
    #ifdef __amd64__
    alloc_chunkwise(ALLOC_SPARSE | ALLOC_DENSE),
    #else
    alloc_chunkwise(ALLOC_SPARSE),
    #endif
    num_procs(1), droptemp(0),  unlogged(0), hstore_match_only(0), flat_node_cache_enabled(0), excludepoly(0), flat_node_file(NULL),
    tag_transform_script(NULL), create(0), sanitize(0), long_usage_bool(0), pass_prompt(0), db("gis"), username(NULL), host(NULL),
    password(NULL), port("5432"), output_backend("pgsql"), input_reader("auto"), bbox(NULL), extra_attributes(0), verbose(0)
{

}

options_t::~options_t()
{
}

parse_delegate_t* options_t::create_parser()
{
    return new parse_delegate_t(extra_attributes, bbox, projection);
}

middle_t* options_t::create_middle()
{
     return slim ? (middle_t*)new middle_pgsql_t() : (middle_t*)new middle_ram_t();
}

output_t* options_t::create_output(middle_t* mid)
{
    if (strcmp("pgsql", output_backend) == 0) {
        return new output_pgsql_t(mid, this);
    } else if (strcmp("gazetteer", output_backend) == 0) {
        return new output_gazetteer_t(mid, this);
    } else if (strcmp("null", output_backend) == 0) {
        return new output_null_t(mid, this);
    } else {
        throw std::runtime_error((boost::format("Output backend `%1%' not recognised. Should be one of [pgsql, gazetteer, null].\n") % output_backend).str());
    }
}

std::vector<output_t*> options_t::create_outputs(middle_t* mid) {
    std::vector<output_t*> outputs;
    outputs.push_back(create_output(mid));
    return outputs;
}

options_t options_t::parse(int argc, char *argv[])
{
    options_t options;
    const char *temparg;
    int c;

    //keep going while there are args left to handle
    optind = 1;
    while(-1 != (c = getopt_long(argc, argv, short_options, long_options, NULL))) {

        //handle the current arg
        switch (c) {
        case 'a':
            options.append = 1;
            break;
        case 'b':
            options.bbox = optarg;
            break;
        case 'c':
            options.create = 1;
            break;
        case 'v':
            options.verbose = 1;
            break;
        case 's':
            options.slim = 1;
            break;
        case 'K':
            options.keep_coastlines = 1;
            break;
        case 'u':
            options.sanitize = 1;
            break;
        case 'l':
            options.projection.reset(new reprojection(PROJ_LATLONG));
            break;
        case 'm':
            options.projection.reset(new reprojection(PROJ_SPHERE_MERC));
            break;
        case 'M':
            options.projection.reset(new reprojection(PROJ_MERC));
            break;
        case 'E':
            options.projection.reset(new reprojection(-atoi(optarg)));
            break;
        case 'p':
            options.prefix = optarg;
            break;
        case 'd':
            options.db = optarg;
            break;
        case 'C':
            options.cache = atoi(optarg);
            break;
        case 'U':
            options.username = optarg;
            break;
        case 'W':
            options.pass_prompt = 1;
            break;
        case 'H':
            options.host = optarg;
            break;
        case 'P':
            options.port = optarg;
            break;
        case 'S':
            options.style = optarg;
            break;
        case 'i':
            options.tblsmain_index = options.tblsslim_index = optarg;
            break;
        case 200:
            options.tblsslim_data = optarg;
            break;
        case 201:
            options.tblsslim_index = optarg;
            break;
        case 202:
            options.tblsmain_data = optarg;
            break;
        case 203:
            options.tblsmain_index = optarg;
            break;
        case 'e':
            options.expire_tiles_zoom_min = atoi(optarg);
            temparg = strchr(optarg, '-');
            if (temparg)
                options.expire_tiles_zoom = atoi(temparg + 1);
            if (options.expire_tiles_zoom < options.expire_tiles_zoom_min)
                options.expire_tiles_zoom = options.expire_tiles_zoom_min;
            break;
        case 'o':
            options.expire_tiles_filename = optarg;
            break;
        case 'O':
            options.output_backend = optarg;
            break;
        case 'x':
            options.extra_attributes = 1;
            break;
        case 'k':
            if (options.enable_hstore != HSTORE_NONE) {
                throw std::runtime_error("ERROR: You can not specify both --hstore (-k) and --hstore-all (-j)\n");
            }
            options.enable_hstore = HSTORE_NORM;
            break;
        case 208:
            options.hstore_match_only = 1;
            break;
        case 'j':
            if (options.enable_hstore != HSTORE_NONE) {
                throw std::runtime_error("ERROR: You can not specify both --hstore (-k) and --hstore-all (-j)\n");
            }
            options.enable_hstore = HSTORE_ALL;
            break;
        case 'z':
            options.hstore_columns.push_back(optarg);
            break;
        case 'G':
            options.enable_multi = 1;
            break;
        case 'r':
            options.input_reader = optarg;
            break;
        case 'h':
            options.long_usage_bool = 1;
            break;
        case 'I':
#ifdef HAVE_PTHREAD
            options.parallel_indexing = 0;
#endif
            break;
        case 204:
            if (strcmp(optarg, "dense") == 0)
                options.alloc_chunkwise = ALLOC_DENSE;
            else if (strcmp(optarg, "chunk") == 0)
                options.alloc_chunkwise = ALLOC_DENSE | ALLOC_DENSE_CHUNK;
            else if (strcmp(optarg, "sparse") == 0)
                options.alloc_chunkwise = ALLOC_SPARSE;
            else if (strcmp(optarg, "optimized") == 0)
                options.alloc_chunkwise = ALLOC_DENSE | ALLOC_SPARSE;
            else {
                throw std::runtime_error((boost::format("ERROR: Unrecognized cache strategy %1%.\n") % optarg).str());
            }
            break;
        case 205:
#ifdef HAVE_FORK
            options.num_procs = atoi(optarg);
#else
            fprintf(stderr, "WARNING: osm2pgsql was compiled without fork, only using one process!\n");
#endif
            break;
        case 206:
            options.droptemp = 1;
            break;
        case 207:
            options.unlogged = 1;
            break;
        case 209:
            options.flat_node_cache_enabled = 1;
            options.flat_node_file = optarg;
            break;
        case 210:
            options.excludepoly = 1;
            break;
        case 211:
            options.enable_hstore_index = 1;
            break;
        case 212:
            options.tag_transform_script = optarg;
            break;
        case 'V':
            exit (EXIT_SUCCESS);
            break;
        case '?':
        default:
            short_usage(argv[0]);
            break;
        }
    } //end while

    //they were looking for usage info
    if (options.long_usage_bool) {
        long_usage(argv[0], options.verbose);
    }

    //we require some input files!
    if (argc == optind) {
        short_usage(argv[0]);
    }

    //get the input files
    while (optind < argc) {
        options.input_files.push_back(std::string(argv[optind]));
        optind++;
    }

    if (options.append && options.create) {
        throw std::runtime_error("Error: --append and --create options can not be used at the same time!\n");
    }

    if (options.droptemp && !options.slim) {
        throw std::runtime_error("Error: --drop only makes sense with --slim.\n");
    }

    if (options.unlogged && !options.create) {
        fprintf(stderr, "Warning: --unlogged only makes sense with --create; ignored.\n");
        options.unlogged = 0;
    }

    if (options.enable_hstore == HSTORE_NONE && options.hstore_columns.size() == 0 && options.hstore_match_only) {
        fprintf(stderr, "Warning: --hstore-match-only only makes sense with --hstore, --hstore-all, or --hstore-column; ignored.\n");
        options.hstore_match_only = 0;
    }

    if (options.enable_hstore_index && options.enable_hstore == HSTORE_NONE && options.hstore_columns.size() == 0) {
        fprintf(stderr, "Warning: --hstore-add-index only makes sense with hstore enabled.\n");
        options.enable_hstore_index = 0;
    }

    if (options.cache < 0)
        options.cache = 0;

    if (options.cache == 0) {
        fprintf(stderr, "WARNING: ram cache is disabled. This will likely slow down processing a lot.\n\n");
    }
    if (sizeof(int*) == 4 && options.slim != 1) {
        fprintf(stderr, "\n!! You are running this on 32bit system, so at most\n");
        fprintf(stderr, "!! 3GB of RAM can be used. If you encounter unexpected\n");
        fprintf(stderr, "!! exceptions during import, you should try running in slim\n");
        fprintf(stderr, "!! mode using parameter -s.\n");
    }

    if (options.pass_prompt)
        options.password = simple_prompt("Password:", 100, 0);
    else {
        options.password = getenv("PGPASS");
    }

    if (options.num_procs < 1)
        options.num_procs = 1;

    options.scale = (options.projection->get_proj_id() == PROJ_LATLONG) ? 10000000 : 100;
    options.conninfo = build_conninfo(options.db, options.username, options.password, options.host, options.port);

    return options;
}
