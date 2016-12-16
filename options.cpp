#include "options.hpp"
#include "sprompt.hpp"

#include <getopt.h>
#ifdef HAVE_LIBGEN_H
#include <libgen.h>
#else
#define basename /*SKIP IT*/
#endif
#include <stdio.h>
#include <string.h>
#include <stdexcept>
#include <sstream>
#include <thread> // for number of threads
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
        {"expire-bbox-size", 1, 0, 214},
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
        {"reproject-area",0,0,213},
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
        printf("\
                        %s/default.style.\n", OSM2PGSQL_DATADIR);
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
                        sparse: caching strategy optimised for small imports\n\
                        optimized: automatically combines dense and sparse \n\
                            strategies for optimal storage efficiency. This may\n\
                            us twice as much virtual memory, but no more physical \n\
                            memory.\n");
    #ifdef __amd64__
        printf("\
                        The default is \"optimized\"\n");
    #else
        /* use "chunked" as a default in 32 bit compilations, as it is less wasteful of virtual memory than "optimized"*/
        printf("\
                        The default is \"sparse\"\n");
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
          --expire-bbox-size Max size for a polygon to expire the whole polygon,\n\
                             not just the boundary.\n\
    \n\
    Other options:\n\
       -b|--bbox        Apply a bounding box filter on the imported data\n\
                        Must be specified as: minlon,minlat,maxlon,maxlat\n\
                        e.g. --bbox -0.5,51.25,0.5,51.75\n\
       -p|--prefix      Prefix for table names (default planet_osm)\n\
                        The prefix may also contain a schema like in \"geodata.osm\".\n\
       -r|--input-reader    Input format.\n\
                        auto      - Detect file format. (default)\n\
                        o5m       - Parse as o5m format.\n\
                        xml       - Parse as OSM XML.\n\
                        pbf       - OSM binary format.\n\
       -O|--output      Output backend.\n\
                        pgsql - Output to a PostGIS database (default)\n\
                        multi - Multiple Custom Table Output to a PostGIS \n\
                            database (requires style file for configuration)\n\
                        gazetteer - Output to a PostGIS database for Nominatim\n\
                        null - No output. Useful for testing. Still creates tables if --slim is specified.\n");
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
          --exclude-invalid-polygon   do not attempt to recover invalid geometries.\n\
          --reproject-area   compute area column using spherical mercator coordinates.\n\
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
            printf("    osmosis --rri workingDirectory=<osmosis dir> --simc --wxc - \\\n");
            printf("      | %s -a -d gis --slim -k --flat-nodes <flat nodes> -r xml -\n", name);
            printf("where\n");
            printf("    <flat nodes> is the same location as above.\n");
            printf("    <osmosis dir> is the location osmosis replication was initialized to.\n");
            printf("\nRun %s --help --verbose (-h -v) for a full list of options.\n", name);
        }

    }

} // anonymous namespace

database_options_t::database_options_t():
    db("gis"), username(boost::none), host(boost::none),
    password(boost::none), port(boost::none)
{

}

std::string database_options_t::conninfo() const
{
    std::ostringstream out;

    out << "dbname='" << db << "'";

    if (username) {
        out << " user='" << *username << "'";
    }
    if (password) {
        out << " password='" << *password << "'";
    }
    if (host) {
        out << " host='" << *host << "'";
    }
    if (port) {
        out << " port='" << *port << "'";
    }

    return out.str();
}

options_t::options_t():
    prefix("planet_osm"), scale(DEFAULT_SCALE), projection(reprojection::create_projection(PROJ_SPHERE_MERC)), append(false), slim(false),
    cache(800), tblsmain_index(boost::none), tblsslim_index(boost::none), tblsmain_data(boost::none), tblsslim_data(boost::none), style(OSM2PGSQL_DATADIR "/default.style"),
    expire_tiles_zoom(-1), expire_tiles_zoom_min(-1), expire_tiles_max_bbox(20000.0), expire_tiles_filename("dirty_tiles"),
    hstore_mode(HSTORE_NONE), enable_hstore_index(false),
    enable_multi(false), hstore_columns(), keep_coastlines(false), parallel_indexing(true),
    #ifdef __amd64__
    alloc_chunkwise(ALLOC_SPARSE | ALLOC_DENSE),
    #else
    alloc_chunkwise(ALLOC_SPARSE),
    #endif
    droptemp(false),  unlogged(false), hstore_match_only(false), flat_node_cache_enabled(false), excludepoly(false), reproject_area(false), flat_node_file(boost::none),
    tag_transform_script(boost::none), tag_transform_node_func(boost::none), tag_transform_way_func(boost::none),
    tag_transform_rel_func(boost::none), tag_transform_rel_mem_func(boost::none),
    create(false), long_usage_bool(false), pass_prompt(false),  output_backend("pgsql"), input_reader("auto"), bbox(boost::none),
    extra_attributes(false), verbose(false)
{
    num_procs = std::thread::hardware_concurrency();
    if (num_procs < 1) {
        fprintf(stderr, "WARNING: unable to detect number of hardware threads supported!\n");
        num_procs = 1;
    }
}

options_t::~options_t()
{
}

options_t::options_t(int argc, char *argv[]): options_t()
{
    const char *temparg;
    int c;

    //keep going while there are args left to handle
    // note: optind would seem to need to be set to 1, but that gives valgrind
    // errors - setting it to zero seems to work, though. see
    // http://stackoverflow.com/questions/15179963/is-it-possible-to-repeat-getopt#15179990
    optind = 0;
    while(-1 != (c = getopt_long(argc, argv, short_options, long_options, nullptr))) {

        //handle the current arg
        switch (c) {
        case 'a':
            append = true;
            break;
        case 'b':
            bbox = optarg;
            break;
        case 'c':
            create = true;
            break;
        case 'v':
            verbose = true;
            break;
        case 's':
            slim = true;
            break;
        case 'K':
            keep_coastlines = true;
            break;
        case 'l':
            projection.reset(reprojection::create_projection(PROJ_LATLONG));
            break;
        case 'm':
            projection.reset(reprojection::create_projection(PROJ_SPHERE_MERC));
            break;
        case 'E':
            projection.reset(reprojection::create_projection(atoi(optarg)));
            break;
        case 'p':
            prefix = optarg;
            break;
        case 'd':
            database_options.db = optarg;
            break;
        case 'C':
            cache = atoi(optarg);
            break;
        case 'U':
            database_options.username = optarg;
            break;
        case 'W':
            pass_prompt = true;
            break;
        case 'H':
            database_options.host = optarg;
            break;
        case 'P':
            database_options.port = optarg;
            break;
        case 'S':
            style = optarg;
            break;
        case 'i':
            tblsmain_index = tblsslim_index = optarg;
            break;
        case 200:
            tblsslim_data = optarg;
            break;
        case 201:
            tblsslim_index = optarg;
            break;
        case 202:
            tblsmain_data = optarg;
            break;
        case 203:
            tblsmain_index = optarg;
            break;
        case 'e':
            expire_tiles_zoom_min = atoi(optarg);
            temparg = strchr(optarg, '-');
            if (temparg)
                expire_tiles_zoom = atoi(temparg + 1);
            if (expire_tiles_zoom < expire_tiles_zoom_min)
                expire_tiles_zoom = expire_tiles_zoom_min;
            break;
        case 'o':
            expire_tiles_filename = optarg;
            break;
        case 214:
            expire_tiles_max_bbox = atof(optarg);
            break;
        case 'O':
            output_backend = optarg;
            break;
        case 'x':
            extra_attributes = true;
            break;
        case 'k':
            if (hstore_mode != HSTORE_NONE) {
                throw std::runtime_error("You can not specify both --hstore (-k) and --hstore-all (-j)\n");
            }
            hstore_mode = HSTORE_NORM;
            break;
        case 208:
            hstore_match_only = true;
            break;
        case 'j':
            if (hstore_mode != HSTORE_NONE) {
                throw std::runtime_error("You can not specify both --hstore (-k) and --hstore-all (-j)\n");
            }
            hstore_mode = HSTORE_ALL;
            break;
        case 'z':
            hstore_columns.push_back(optarg);
            break;
        case 'G':
            enable_multi = true;
            break;
        case 'r':
            input_reader = optarg;
            break;
        case 'h':
            long_usage_bool = true;
            break;
        case 'I':
            parallel_indexing = false;
            break;
        case 204:
            if (strcmp(optarg, "dense") == 0)
                alloc_chunkwise = ALLOC_DENSE;
            else if (strcmp(optarg, "chunk") == 0)
                alloc_chunkwise = ALLOC_DENSE | ALLOC_DENSE_CHUNK;
            else if (strcmp(optarg, "sparse") == 0)
                alloc_chunkwise = ALLOC_SPARSE;
            else if (strcmp(optarg, "optimized") == 0)
                alloc_chunkwise = ALLOC_DENSE | ALLOC_SPARSE;
            else {
                throw std::runtime_error((boost::format("Unrecognized cache strategy %1%.\n") % optarg).str());
            }
            break;
        case 205:
            num_procs = atoi(optarg);
            break;
        case 206:
            droptemp = true;
            break;
        case 207:
            unlogged = true;
            break;
        case 209:
            flat_node_cache_enabled = true;
            flat_node_file = optarg;
            break;
        case 210:
            excludepoly = true;
            break;
        case 211:
            enable_hstore_index = true;
            break;
        case 212:
            tag_transform_script = optarg;
            break;
        case 213:
            reproject_area = true;
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
    if (long_usage_bool) {
        long_usage(argv[0], verbose);
        return;
    }

    //we require some input files!
    if (argc == optind) {
        short_usage(argv[0]);
    }

    //get the input files
    while (optind < argc) {
        input_files.push_back(std::string(argv[optind]));
        optind++;
    }

    check_options();

    if (pass_prompt) {
        char *prompt = simple_prompt("Password:", 100, 0);
        if (prompt == nullptr) {
            database_options.password = boost::none;
        } else {
            database_options.password = std::string(prompt);
        }
    }


    //NOTE: this is hugely important if you set it inappropriately and are are caching nodes
    //you could get overflow when working with larger coordinates (mercator) and larger scales
    scale = (projection->target_latlon()) ? 10000000 : 100;

    // handle schema in prefix
    size_t offset;
    offset = prefix.find(".");
    if (offset == std::string::npos) {
        prefix_schema = ""; // we don't assume any schema
        prefix_table = prefix;
    } else {
        prefix_schema = prefix.substr(0, offset);;
        prefix_table = prefix.substr(offset+1);
    }
}

void options_t::check_options()
{
    if (append && create) {
        throw std::runtime_error("--append and --create options can not be used at the same time!\n");
    }

    if (append && !slim) {
        throw std::runtime_error("--append can only be used with slim mode!\n");
    }

    if (droptemp && !slim) {
        throw std::runtime_error("--drop only makes sense with --slim.\n");
    }

    if (unlogged && !create) {
        fprintf(stderr, "Warning: --unlogged only makes sense with --create; ignored.\n");
        unlogged = false;
    }

    if (hstore_mode == HSTORE_NONE && hstore_columns.size() == 0 && hstore_match_only) {
        fprintf(stderr, "Warning: --hstore-match-only only makes sense with --hstore, --hstore-all, or --hstore-column; ignored.\n");
        hstore_match_only = false;
    }

    if (enable_hstore_index && hstore_mode == HSTORE_NONE && hstore_columns.size() == 0) {
        fprintf(stderr, "Warning: --hstore-add-index only makes sense with hstore enabled.\n");
        enable_hstore_index = false;
    }

    if (cache < 0) {
        cache = 0;
        fprintf(stderr, "WARNING: ram cache cannot be negative. Using 0 instead.\n\n");
    }

    if (cache == 0) {
        fprintf(stderr, "WARNING: ram cache is disabled. This will likely slow down processing a lot.\n\n");
    }

    if (num_procs < 1) {
        num_procs = 1;
        fprintf(stderr, "WARNING: Must use at least 1 process.\n\n");
    }

    if (sizeof(int*) == 4 && !slim) {
        fprintf(stderr, "\n!! You are running this on 32bit system, so at most\n");
        fprintf(stderr, "!! 3GB of RAM can be used. If you encounter unexpected\n");
        fprintf(stderr, "!! exceptions during import, you should try running in slim\n");
        fprintf(stderr, "!! mode using parameter -s.\n");
    }
}
