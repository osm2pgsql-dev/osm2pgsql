#include "options.hpp"
#include "sprompt.hpp"

#include <getopt.h>
#ifdef HAVE_LIBGEN_H
#include <libgen.h>
#else
#define basename /*SKIP IT*/
#endif
#include <boost/format.hpp>
#include <cstdio>
#include <cstring>
#include <osmium/version.hpp>
#include <sstream>
#include <stdexcept>
#include <thread> // for number of threads

#ifdef HAVE_LUA
extern "C" {
#include <lua.h>
}
#endif

#ifdef HAVE_LUAJIT
extern "C" {
#include <luajit.h>
}
#endif


namespace
{
    const char * short_options = "ab:cd:KhlmMp:suvU:WH:P:i:IE:C:S:e:o:O:xkjGz:r:VF:";
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
        {"flat-nodes",1,0, 'F'},
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
                        %s.\n", DEFAULT_STYLE);
        printf("%s", "\
       -C|--cache       Use up to this many MB for caching nodes (default: 800)\n\
       -F|--flat-nodes  Specifies the flat file to use to persistently store node \n\
                        information in slim mode instead of in PostgreSQL.\n\
                        This file is a single > 40Gb large file. Only recommended\n\
                        for full planet imports. Default is disabled.\n\
    \n\
    Database options:\n\
       -d|--database    The name of the PostgreSQL database to connect to.\n\
       -U|--username    PostgreSQL user name (specify passsword in PGPASS\n\
                        environment variable or use -W).\n\
       -W|--password    Force password prompt.\n\
       -H|--host        Database server host name or socket location.\n\
       -P|--port        Database server port.\n");

        if (verbose)
        {
            printf("%s", "\
    \n\
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
    \n\
    Expiry options:\n\
       -e|--expire-tiles [min_zoom-]max_zoom    Create a tile expiry list.\n\
                             Zoom levels must be larger than 0 and smaller\n\
                             than 32.\n\
       -o|--expire-output filename  Output filename for expired tiles list.\n\
          --expire-bbox-size Max size for a polygon to expire the whole polygon,\n\
                             not just the boundary.\n\
    \n\
    Other options:\n\
       -b|--bbox        Apply a bounding box filter on the imported data\n\
                        Must be specified as: minlon,minlat,maxlon,maxlat\n\
                        e.g. --bbox -0.5,51.25,0.5,51.75\n\
       -p|--prefix      Prefix for table names (default planet_osm)\n\
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
            printf("    <cache size> is 50000 on machines with 64GB or more RAM \n");
            printf("      or about 75%% of memory in MB on machines with less\n");
            printf("    <flat nodes> is a location where a 50+GB file can be saved.\n");
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
    db(boost::none), username(boost::none), host(boost::none),
    password(boost::none), port(boost::none)
{

}

std::string database_options_t::conninfo() const
{
    std::ostringstream out;

    out << "fallback_application_name='osm2pgsql'";
    if (db) {
        out << " dbname='" << *db << "'";
    }
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

options_t::options_t()
: prefix("planet_osm"),
  projection(reprojection::create_projection(PROJ_SPHERE_MERC)), append(false),
  slim(false), cache(800), tblsmain_index(boost::none),
  tblsslim_index(boost::none), tblsmain_data(boost::none),
  tblsslim_data(boost::none), style(DEFAULT_STYLE),
  expire_tiles_zoom(0), expire_tiles_zoom_min(0),
  expire_tiles_max_bbox(20000.0), expire_tiles_filename("dirty_tiles"),
  hstore_mode(HSTORE_NONE), enable_hstore_index(false), enable_multi(false),
  hstore_columns(), keep_coastlines(false), parallel_indexing(true),
#ifdef __amd64__
  alloc_chunkwise(ALLOC_SPARSE | ALLOC_DENSE),
#else
  alloc_chunkwise(ALLOC_SPARSE),
#endif
  droptemp(false), hstore_match_only(false),
  flat_node_cache_enabled(false), reproject_area(false),
  flat_node_file(boost::none), tag_transform_script(boost::none),
  tag_transform_node_func(boost::none), tag_transform_way_func(boost::none),
  tag_transform_rel_func(boost::none), tag_transform_rel_mem_func(boost::none),
  create(false), long_usage_bool(false), pass_prompt(false),
  output_backend("pgsql"), input_reader("auto"), bbox(boost::none),
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
            if (!optarg || optarg[0] == '-') {
                throw std::runtime_error("Missing argument for option --expire-tiles. Zoom "
                                         "levels must be positive.\n");
            }
            char *next_char;
            expire_tiles_zoom_min =
                static_cast<uint32_t>(std::strtoul(optarg, &next_char, 10));
            if (expire_tiles_zoom_min == 0) {
                throw std::runtime_error("Bad argument for option --expire-tiles. "
                                         "Minimum zoom level must be larger "
                                         "than 0.\n");
            }
            // The first character after the number is ignored because that is the separating hyphen.
            if (*next_char == '-') {
                ++next_char;
                // Second number must not be negative because zoom levels must be positive.
                if (next_char && *next_char != '-' && isdigit(*next_char)) {
                    char *after_maxzoom;
                    expire_tiles_zoom = static_cast<uint32_t>(
                        std::strtoul(next_char, &after_maxzoom, 10));
                    if (expire_tiles_zoom == 0 || *after_maxzoom != '\0') {
                        throw std::runtime_error("Invalid maximum zoom level "
                                                 "given for tile expiry.\n");
                    }
                } else {
                    throw std::runtime_error(
                        "Invalid maximum zoom level given for tile expiry.\n");
                }
            } else if (*next_char == '\0') {
                // end of string, no second zoom level given
                expire_tiles_zoom = expire_tiles_zoom_min;
            } else {
                throw std::runtime_error("Minimum and maximum zoom level for "
                                         "tile expiry must be separated by "
                                         "'-'.\n");
            }
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
        case 'F':
            flat_node_cache_enabled = true;
            flat_node_file = optarg;
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
            fprintf(stderr, "Compiled using the following library versions:\n");
            fprintf(stderr, "Libosmium %s\n", LIBOSMIUM_VERSION_STRING);
#ifdef HAVE_LUA
            fprintf(stderr, "%s", LUA_RELEASE);
#ifdef HAVE_LUAJIT
            fprintf(stderr, " (%s)", LUAJIT_VERSION);
#endif
#else
            fprintf(stderr, "Lua support not included");
#endif
            fprintf(stderr, "\n");
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
        if (!slim) {
            throw std::runtime_error(
                "Ram node cache can only be disable in slim mode.\n");
        }
        if (!flat_node_cache_enabled) {
            fprintf(stderr, "WARNING: ram cache is disabled. This will likely "
                            "slow down processing a lot.\n\n");
        }
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

    // zoom level 31 is the technical limit because we use 32-bit integers for the x and y index of a tile ID
    if (expire_tiles_zoom_min >= 32) {
        expire_tiles_zoom_min = 31;
        fprintf(stderr, "WARNING: minimum zoom level for tile expiry is too "
                        "large and has been set to 31.\n\n");
    }

    if (expire_tiles_zoom >= 32) {
        expire_tiles_zoom = 31;
        fprintf(stderr, "WARNING: maximum zoom level for tile expiry is too "
                        "large and has been set to 31.\n\n");
    }
}
