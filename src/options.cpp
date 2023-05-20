/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "format.hpp"
#include "logging.hpp"
#include "options.hpp"
#include "pgsql.hpp"
#include "reprojection.hpp"
#include "util.hpp"
#include "version.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <getopt.h>
#include <osmium/version.hpp>
#include <stdexcept>
#include <thread> // for number of threads

#ifdef HAVE_LUA
extern "C"
{
#include <lua.h>
}
#endif

#ifdef HAVE_LUAJIT
extern "C"
{
#include <luajit.h>
}
#endif

static char const *program_name(char const *name)
{
    char const *const slash = std::strrchr(name, '/');
    return slash ? (slash + 1) : name;
}

namespace {
char const *const short_options =
    "ab:cd:KhlmMp:suvU:WH:P:i:IE:C:S:e:o:O:xkjGz:r:VF:";

struct option const long_options[] = {
    {"append", no_argument, nullptr, 'a'},
    {"bbox", required_argument, nullptr, 'b'},
    {"cache", required_argument, nullptr, 'C'},
    {"cache-strategy", required_argument, nullptr, 204},
    {"create", no_argument, nullptr, 'c'},
    {"database", required_argument, nullptr, 'd'},
    {"disable-parallel-indexing", no_argument, nullptr, 'I'},
    {"drop", no_argument, nullptr, 206},
    {"expire-bbox-size", required_argument, nullptr, 214},
    {"expire-output", required_argument, nullptr, 'o'},
    {"expire-tiles", required_argument, nullptr, 'e'},
    {"extra-attributes", no_argument, nullptr, 'x'},
    {"flat-nodes", required_argument, nullptr, 'F'},
    {"help", no_argument, nullptr, 'h'},
    {"host", required_argument, nullptr, 'H'},
    {"hstore", no_argument, nullptr, 'k'},
    {"hstore-add-index", no_argument, nullptr, 211},
    {"hstore-all", no_argument, nullptr, 'j'},
    {"hstore-column", required_argument, nullptr, 'z'},
    {"hstore-match-only", no_argument, nullptr, 208},
    {"input-reader", required_argument, nullptr, 'r'},
    {"keep-coastlines", no_argument, nullptr, 'K'},
    {"latlong", no_argument, nullptr, 'l'},
    {"log-level", required_argument, nullptr, 400},
    {"log-progress", required_argument, nullptr, 401},
    {"log-sql", no_argument, nullptr, 402},
    {"log-sql-data", no_argument, nullptr, 403},
    {"merc", no_argument, nullptr, 'm'},
    {"middle-schema", required_argument, nullptr, 215},
    {"middle-way-node-index-id-shift", required_argument, nullptr, 300},
    {"multi-geometry", no_argument, nullptr, 'G'},
    {"number-processes", required_argument, nullptr, 205},
    {"output", required_argument, nullptr, 'O'},
    {"output-pgsql-schema", required_argument, nullptr, 216},
    {"password", no_argument, nullptr, 'W'},
    {"port", required_argument, nullptr, 'P'},
    {"prefix", required_argument, nullptr, 'p'},
    {"proj", required_argument, nullptr, 'E'},
    {"reproject-area", no_argument, nullptr, 213},
    {"slim", no_argument, nullptr, 's'},
    {"style", required_argument, nullptr, 'S'},
    {"tablespace-index", required_argument, nullptr, 'i'},
    {"tablespace-main-data", required_argument, nullptr, 202},
    {"tablespace-main-index", required_argument, nullptr, 203},
    {"tablespace-slim-data", required_argument, nullptr, 200},
    {"tablespace-slim-index", required_argument, nullptr, 201},
    {"tag-transform-script", required_argument, nullptr, 212},
    {"username", required_argument, nullptr, 'U'},
    {"verbose", no_argument, nullptr, 'v'},
    {"version", no_argument, nullptr, 'V'},
    {"with-forward-dependencies", required_argument, nullptr, 217},
    {nullptr, 0, nullptr, 0}};

void long_usage(char const *arg0, bool verbose)
{
    char const *const name = program_name(arg0);

    fmt::print(stdout, "\nUsage: {} [OPTIONS] OSM-FILE...\n", name);
    (void)std::fputs(
        "\nImport data from the OSM file(s) into a PostgreSQL database.\n\n\
Full documentation is available at https://osm2pgsql.org/\n\n",
        stdout);

    (void)std::fputs("\
Common options:\n\
    -a|--append     Update existing osm2pgsql database with data from file.\n\
    -c|--create     Import OSM data from file into database. This is the\n\
                    default if --append is not specified.\n\
    -O|--output=OUTPUT  Set output. Options are:\n\
                    pgsql - Output to a PostGIS database (default)\n\
                    flex - More flexible output to PostGIS database\n\
                    gazetteer - Output to a PostGIS database for Nominatim\n\
                                (deprecated)\n\
                    null - No output. Used for testing.\n\
    -S|--style=FILE  Location of the style file. Defaults to\n\
                    '" DEFAULT_STYLE "'.\n\
    -k|--hstore     Add tags without column to an additional hstore column.\n",
               stdout);
#ifdef HAVE_LUA
    (void)std::fputs("\
       --tag-transform-script=SCRIPT  Specify a Lua script to handle tag\n\
                    filtering and normalisation (pgsql output only).\n",
               stdout);
#endif
    (void)std::fputs("\
    -s|--slim       Store temporary data in the database. This switch is\n\
                    required if you want to update with --append later.\n\
        --drop      Only with --slim: drop temporary tables after import\n\
                    (no updates are possible).\n\
    -C|--cache=SIZE  Use up to SIZE MB for caching nodes (default: 800).\n\
    -F|--flat-nodes=FILE  Specifies the file to use to persistently store node\n\
                    information in slim mode instead of in PostgreSQL.\n\
                    This is a single large file (> 50GB). Only recommended\n\
                    for full planet imports. Default is disabled.\n\
\n\
Database options:\n\
    -d|--database=DB  The name of the PostgreSQL database to connect to or\n\
                    a PostgreSQL conninfo string.\n\
    -U|--username=NAME  PostgreSQL user name.\n\
    -W|--password   Force password prompt.\n\
    -H|--host=HOST  Database server host name or socket location.\n\
    -P|--port=PORT  Database server port.\n",
               stdout);

    if (verbose) {
        (void)std::fputs("\n\
Logging options:\n\
       --log-level=LEVEL  Set log level ('debug', 'info' (default), 'warn',\n\
                    or 'error').\n\
       --log-progress=VALUE  Enable ('true') or disable ('false') progress\n\
                    logging. If set to 'auto' osm2pgsql will enable progress\n\
                    logging on the console and disable it if the output is\n\
                    redirected to a file. Default: true.\n\
       --log-sql    Enable logging of SQL commands for debugging.\n\
       --log-sql-data  Enable logging of all data added to the database.\n\
    -v|--verbose    Same as '--log-level=debug'.\n\
\n\
Input options:\n\
    -r|--input-reader=FORMAT  Input format ('xml', 'pbf', 'o5m', or\n\
                    'auto' - autodetect format (default))\n\
    -b|--bbox=MINLON,MINLAT,MAXLON,MAXLAT  Apply a bounding box filter on the\n\
                    imported data, e.g. '--bbox -0.5,51.25,0.5,51.75'.\n\
\n\
Middle options:\n\
    -i|--tablespace-index=TBLSPC  The name of the PostgreSQL tablespace where\n\
                    all indexes will be created.\n\
                    The following options allow more fine-grained control:\n\
       --tablespace-slim-data=TBLSPC  Tablespace for slim mode tables.\n\
       --tablespace-slim-index=TBLSPC  Tablespace for slim mode indexes.\n\
                    (if unset, use db's default; -i is equivalent to setting\n\
                    --tablespace-main-index and --tablespace-slim-index).\n\
    -p|--prefix=PREFIX  Prefix for table names (default 'planet_osm')\n\
       --cache-strategy=STRATEGY  Deprecated. Not used any more.\n\
    -x|--extra-attributes  Include attributes (user name, user id, changeset\n\
                    id, timestamp and version) for each object in the database.\n\
       --middle-schema=SCHEMA  Schema to use for middle tables (default: none).\n\
       --middle-way-node-index-id-shift=SHIFT  Set ID shift for bucket index.\n\
\n\
Pgsql output options:\n\
    -i|--tablespace-index=TBLSPC  The name of the PostgreSQL tablespace where\n\
                    all indexes will be created.\n\
                    The following options allow more fine-grained control:\n\
       --tablespace-main-data=TBLSPC  Tablespace for main tables.\n\
       --tablespace-main-index=TBLSPC  Tablespace for main table indexes.\n\
    -l|--latlong    Store data in degrees of latitude & longitude (WGS84).\n\
    -m|--merc       Store data in web mercator (default).\n"
#ifdef HAVE_GENERIC_PROJ
                   "    -E|--proj=SRID  Use projection EPSG:SRID.\n"
#endif
                   "\
    -p|--prefix=PREFIX  Prefix for table names (default 'planet_osm').\n\
    -x|--extra-attributes  Include attributes (user name, user id, changeset\n\
                    id, timestamp and version) for each object in the database.\n\
       --hstore-match-only  Only keep objects that have a value in one of the\n\
                    columns (default with --hstore is to keep all objects).\n\
    -j|--hstore-all  Add all tags to an additional hstore (key/value) column.\n\
    -z|--hstore-column=NAME  Add an additional hstore (key/value) column\n\
                    containing all tags that start with the specified string,\n\
                    eg '--hstore-column name:' will produce an extra hstore\n\
                    column that contains all 'name:xx' tags.\n\
       --hstore-add-index  Add index to hstore column.\n\
    -G|--multi-geometry  Generate multi-geometry features in postgresql tables.\n\
    -K|--keep-coastlines  Keep coastline data rather than filtering it out.\n\
                    Default: discard objects tagged natural=coastline.\n\
       --output-pgsql-schema=SCHEMA Schema to use for pgsql output tables\n\
                    (default: none).\n\
       --reproject-area  Compute area column using web mercator coordinates.\n\
\n\
Expiry options:\n\
    -e|--expire-tiles=[MIN_ZOOM-]MAX_ZOOM  Create a tile expiry list.\n\
                    Zoom levels must be larger than 0 and smaller than 32.\n\
    -o|--expire-output=FILENAME  Output filename for expired tiles list.\n\
       --expire-bbox-size=SIZE  Max size for a polygon to expire the whole\n\
                    polygon, not just the boundary.\n\
\n\
Advanced options:\n\
    -I|--disable-parallel-indexing   Disable indexing all tables concurrently.\n\
       --number-processes=NUM  Specifies the number of parallel processes used\n\
                   for certain operations (default depends on number of CPUs).\n\
       --with-forward-dependencies=BOOL  Propagate changes from nodes to ways\n\
                   and node/way members to relations (Default: true).\n\
",
                   stdout);
    } else {
        fmt::print(
            stdout,
            "\nRun '{} --help --verbose' (-h -v) for a full list of options.\n",
            name);
    }
}

} // anonymous namespace

static bool compare_prefix(std::string const &str,
                           std::string const &prefix) noexcept
{
    return std::strncmp(str.c_str(), prefix.c_str(), prefix.size()) == 0;
}

std::string build_conninfo(database_options_t const &opt)
{
    if (compare_prefix(opt.db, "postgresql://") ||
        compare_prefix(opt.db, "postgres://")) {
        return opt.db;
    }

    util::string_joiner_t joiner{' '};
    joiner.add("fallback_application_name='osm2pgsql'");

    if (std::strchr(opt.db.c_str(), '=') != nullptr) {
        joiner.add(opt.db);
        return joiner();
    }

    joiner.add("client_encoding='UTF8'");

    if (!opt.db.empty()) {
        joiner.add(fmt::format("dbname='{}'", opt.db));
    }
    if (!opt.username.empty()) {
        joiner.add(fmt::format("user='{}'", opt.username));
    }
    if (!opt.password.empty()) {
        joiner.add(fmt::format("password='{}'", opt.password));
    }
    if (!opt.host.empty()) {
        joiner.add(fmt::format("host='{}'", opt.host));
    }
    if (!opt.port.empty()) {
        joiner.add(fmt::format("port='{}'", opt.port));
    }

    return joiner();
}

options_t::options_t()
: num_procs(std::min(4U, std::thread::hardware_concurrency()))
{
    if (num_procs < 1) {
        log_warn("Unable to detect number of hardware threads supported!"
                 " Using single thread.");
        num_procs = 1;
    }
}

static osmium::Box parse_bbox_param(char const *arg)
{
    double minx = NAN;
    double maxx = NAN;
    double miny = NAN;
    double maxy = NAN;

    int const n = sscanf(arg, "%lf,%lf,%lf,%lf", &minx, &miny, &maxx, &maxy);
    if (n != 4) {
        throw std::runtime_error{"Bounding box must be specified like: "
                                 "minlon,minlat,maxlon,maxlat."};
    }

    if (maxx <= minx) {
        throw std::runtime_error{
            "Bounding box failed due to maxlon <= minlon."};
    }

    if (maxy <= miny) {
        throw std::runtime_error{
            "Bounding box failed due to maxlat <= minlat."};
    }

    log_debug("Applying bounding box: {},{} to {},{}", minx, miny, maxx, maxy);

    return osmium::Box{minx, miny, maxx, maxy};
}

static unsigned int parse_number_processes_param(char const *arg)
{
    int num = atoi(arg);
    if (num < 1) {
        log_warn("--number-processes must be at least 1. Using 1.");
        num = 1;
    } else if (num > 32) {
        // The threads will open up database connections which will
        // run out at some point. It depends on the number of tables
        // how many connections there are. The number 32 is way beyond
        // anything that will make sense here.
        log_warn("--number-processes too large. Set to 32.");
        num = 32;
    }

    return static_cast<unsigned int>(num);
}

static void parse_expire_tiles_param(char const *arg,
                                     uint32_t *expire_tiles_zoom_min,
                                     uint32_t *expire_tiles_zoom)
{
    if (!arg || arg[0] == '-') {
        throw std::runtime_error{"Missing argument for option --expire-tiles."
                                 " Zoom levels must be positive."};
    }

    char *next_char = nullptr;
    *expire_tiles_zoom_min =
        static_cast<uint32_t>(std::strtoul(arg, &next_char, 10));

    if (*expire_tiles_zoom_min == 0) {
        throw std::runtime_error{"Bad argument for option --expire-tiles."
                                 " Minimum zoom level must be larger than 0."};
    }

    // The first character after the number is ignored because that is the
    // separating hyphen.
    if (*next_char == '-') {
        ++next_char;
        // Second number must not be negative because zoom levels must be
        // positive.
        if (next_char && *next_char != '-' && isdigit(*next_char)) {
            char *after_maxzoom = nullptr;
            *expire_tiles_zoom = static_cast<uint32_t>(
                std::strtoul(next_char, &after_maxzoom, 10));
            if (*expire_tiles_zoom == 0 || *after_maxzoom != '\0') {
                throw std::runtime_error{"Invalid maximum zoom level"
                                         " given for tile expiry."};
            }
        } else {
            throw std::runtime_error{
                "Invalid maximum zoom level given for tile expiry."};
        }
        return;
    }

    if (*next_char == '\0') {
        // end of string, no second zoom level given
        *expire_tiles_zoom = *expire_tiles_zoom_min;
        return;
    }

    throw std::runtime_error{"Minimum and maximum zoom level for"
                             " tile expiry must be separated by '-'."};
}

static void parse_log_level_param(char const *arg)
{
    if (std::strcmp(arg, "debug") == 0) {
        get_logger().set_level(log_level::debug);
    } else if (std::strcmp(arg, "info") == 0) {
        get_logger().set_level(log_level::info);
    } else if ((std::strcmp(arg, "warn") == 0) ||
               (std::strcmp(arg, "warning") == 0)) {
        get_logger().set_level(log_level::warn);
    } else if (std::strcmp(arg, "error") == 0) {
        get_logger().set_level(log_level::error);
    } else {
        throw fmt_error("Unknown value for --log-level option: {}", arg);
    }
}

static void parse_log_progress_param(char const *arg)
{
    if (std::strcmp(arg, "true") == 0) {
        get_logger().enable_progress();
    } else if (std::strcmp(arg, "false") == 0) {
        get_logger().disable_progress();
    } else if (std::strcmp(arg, "auto") == 0) {
        get_logger().auto_progress();
    } else {
        throw fmt_error("Unknown value for --log-progress option: {}", arg);
    }
}

static bool parse_with_forward_dependencies_param(char const *arg)
{
    log_warn("The option --with-forward-dependencies is deprecated and will "
             "soon be removed.");

    if (std::strcmp(arg, "false") == 0) {
        return false;
    }

    if (std::strcmp(arg, "true") == 0) {
        return true;
    }

    throw fmt_error("Unknown value for --with-forward-dependencies option: {}",
                    arg);
}

static void print_version()
{
    fmt::print(stderr, "Build: {}\n", get_build_type());
    fmt::print(stderr, "Compiled using the following library versions:\n");
    fmt::print(stderr, "Libosmium {}\n", LIBOSMIUM_VERSION_STRING);
    fmt::print(stderr, "Proj {}\n", get_proj_version());
#ifdef HAVE_LUA
#ifdef HAVE_LUAJIT
    fmt::print(stderr, "{} ({})\n", LUA_RELEASE, LUAJIT_VERSION);
#else
    fmt::print(stderr, "{}\n", LUA_RELEASE);
#endif
#else
    fmt::print(stderr, "Lua support not included\n");
#endif
}

options_t::options_t(int argc, char *argv[]) : options_t()
{
    // If there are no command line arguments at all, show help.
    if (argc == 1) {
        m_print_help = true;
        long_usage(argv[0], false);
        return;
    }

    database_options_t database_options;
    bool help_verbose = false; // Will be set when -v/--verbose is set

    int c = 0;

    //keep going while there are args left to handle
    // note: optind would seem to need to be set to 1, but that gives valgrind
    // errors - setting it to zero seems to work, though. see
    // http://stackoverflow.com/questions/15179963/is-it-possible-to-repeat-getopt#15179990
    optind = 0;
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    while (-1 != (c = getopt_long(argc, argv, short_options, long_options,
                                  nullptr))) {

        //handle the current arg
        switch (c) {
        case 'a': // --append
            append = true;
            break;
        case 'b': // --bbox
            bbox = parse_bbox_param(optarg);
            break;
        case 'c': // --create
            create = true;
            break;
        case 'v': // --verbose
            help_verbose = true;
            get_logger().set_level(log_level::debug);
            break;
        case 's': // --slim
            slim = true;
            break;
        case 'K': // --keep-coastlines
            keep_coastlines = true;
            break;
        case 'l': // --latlong
            projection = reprojection::create_projection(PROJ_LATLONG);
            break;
        case 'm': // --merc
            projection = reprojection::create_projection(PROJ_SPHERE_MERC);
            break;
        case 'E': // --proj
#ifdef HAVE_GENERIC_PROJ
            projection = reprojection::create_projection(atoi(optarg));
#else
            throw std::runtime_error{"Generic projections not available."};
#endif
            break;
        case 'p': // --prefix
            prefix = optarg;
            prefix_is_set = true;
            check_identifier(prefix, "--prefix parameter");
            break;
        case 'd': // --database
            database_options.db = optarg;
            break;
        case 'C': // --cache
            cache = atoi(optarg);
            break;
        case 'U': // --username
            database_options.username = optarg;
            break;
        case 'W': // --password
            pass_prompt = true;
            break;
        case 'H': // --host
            database_options.host = optarg;
            break;
        case 'P': // --port
            database_options.port = optarg;
            break;
        case 'S': // --style
            style = optarg;
            break;
        case 'i': // --tablespace-index
            tblsmain_index = optarg;
            tblsslim_index = tblsmain_index;
            break;
        case 200: // --tablespace-slim-data
            tblsslim_data = optarg;
            break;
        case 201: // --tablespace-slim-index
            tblsslim_index = optarg;
            break;
        case 202: // --tablespace-main-data
            tblsmain_data = optarg;
            break;
        case 203: // --tablespace-main-index
            tblsmain_index = optarg;
            break;
        case 'e': // --expire-tiles
            parse_expire_tiles_param(optarg, &expire_tiles_zoom_min,
                                     &expire_tiles_zoom);
            break;
        case 'o': // --expire-output
            expire_tiles_filename = optarg;
            break;
        case 214: // --expire-bbox-size
            expire_tiles_max_bbox = atof(optarg);
            break;
        case 'O': // --output
            output_backend = optarg;
            break;
        case 'x': // --extra-attributes
            extra_attributes = true;
            break;
        case 'k': // --hstore
            if (hstore_mode != hstore_column::none) {
                throw std::runtime_error{"You can not specify both --hstore "
                                         "(-k) and --hstore-all (-j)."};
            }
            hstore_mode = hstore_column::norm;
            break;
        case 208: // --hstore-match-only
            hstore_match_only = true;
            break;
        case 'j': // --hstore-all
            if (hstore_mode != hstore_column::none) {
                throw std::runtime_error{"You can not specify both --hstore "
                                         "(-k) and --hstore-all (-j)."};
            }
            hstore_mode = hstore_column::all;
            break;
        case 'z': // --hstore-column
            hstore_columns.emplace_back(optarg);
            break;
        case 'G': // --multi-geometry
            enable_multi = true;
            break;
        case 'r': // --input-reader
            if (std::strcmp(optarg, "auto") != 0) {
                input_format = optarg;
            }
            break;
        case 'h': // --help
            m_print_help = true;
            break;
        case 'I': // --disable-parallel-indexing
            parallel_indexing = false;
            break;
        case 204: // -cache-strategy
            log_warn("Deprecated option --cache-strategy ignored");
            break;
        case 205: // --number-processes
            num_procs = parse_number_processes_param(optarg);
            break;
        case 206: // --drop
            droptemp = true;
            break;
        case 'F': // --flat-nodes
            flat_node_file = optarg;
            break;
        case 211: // --hstore-add-index
            enable_hstore_index = true;
            break;
        case 212: // --tag-transform-script
            tag_transform_script = optarg;
            break;
        case 213: // --reproject-area
            reproject_area = true;
            break;
        case 'V': // --version
            print_version();
            std::exit(EXIT_SUCCESS); // NOLINT(concurrency-mt-unsafe)
            break;
        case 215: // --middle-schema
            middle_dbschema = optarg;
            check_identifier(middle_dbschema, "--middle-schema parameter");
            break;
        case 216: // --output-pgsql-schema
            output_dbschema = optarg;
            check_identifier(output_dbschema, "--output-pgsql-schema parameter");
            break;
        case 217: // --with-forward-dependencies=BOOL
            with_forward_dependencies =
                parse_with_forward_dependencies_param(optarg);
            break;
        case 300: // --middle-way-node-index-id-shift
            way_node_index_id_shift = atoi(optarg);
            break;
        case 400: // --log-level=LEVEL
            parse_log_level_param(optarg);
            break;
        case 401: // --log-progress=VALUE
            parse_log_progress_param(optarg);
            break;
        case 402: // --log-sql
            get_logger().enable_sql();
            break;
        case 403: // --log-sql-data
            get_logger().enable_sql_data();
            break;
        case '?':
        default:
            throw std::runtime_error{"Usage error. Try 'osm2pgsql --help'."};
        }
    } //end while

    //they were looking for usage info
    if (m_print_help) {
        long_usage(argv[0], help_verbose);
        return;
    }

    //we require some input files!
    if (optind >= argc) {
        throw std::runtime_error{
            "Missing input file(s). Try 'osm2pgsql --help'."};
    }

    //get the input files
    while (optind < argc) {
        input_files.emplace_back(argv[optind]);
        ++optind;
    }

    if (!projection) {
        projection = reprojection::create_projection(PROJ_SPHERE_MERC);
    }

    check_options();

    if (pass_prompt) {
        database_options.password = util::get_password();
    }

    conninfo = build_conninfo(database_options);
}

void options_t::check_options()
{
    if (append && create) {
        throw std::runtime_error{"--append and --create options can not be "
                                 "used at the same time!"};
    }

    if (append && !slim) {
        throw std::runtime_error{"--append can only be used with slim mode!"};
    }

    if (droptemp && !slim) {
        throw std::runtime_error{"--drop only makes sense with --slim."};
    }

    if (hstore_mode == hstore_column::none && hstore_columns.empty() &&
        hstore_match_only) {
        log_warn("--hstore-match-only only makes sense with --hstore, "
                 "--hstore-all, or --hstore-column; ignored.");
        hstore_match_only = false;
    }

    if (enable_hstore_index && hstore_mode == hstore_column::none &&
        hstore_columns.empty()) {
        log_warn("--hstore-add-index only makes sense with hstore enabled; "
                 "ignored.");
        enable_hstore_index = false;
    }

    if (cache < 0) {
        cache = 0;
        log_warn("RAM cache cannot be negative. Using 0 instead.");
    }

    if (cache == 0) {
        if (!slim) {
            throw std::runtime_error{
                "RAM node cache can only be disabled in slim mode."};
        }
        if (flat_node_file.empty()) {
            log_warn("RAM cache is disabled. This will likely slow down "
                     "processing a lot.");
        }
    }

    if (!slim && !flat_node_file.empty()) {
        log_warn("Ignoring --flat-nodes/-F setting in non-slim mode");
    }

    // zoom level 31 is the technical limit because we use 32-bit integers for the x and y index of a tile ID
    if (expire_tiles_zoom_min > 31) {
        expire_tiles_zoom_min = 31;
        log_warn("Minimum zoom level for tile expiry is too "
                 "large and has been set to 31.");
    }

    if (expire_tiles_zoom > 31) {
        expire_tiles_zoom = 31;
        log_warn("Maximum zoom level for tile expiry is too "
                 "large and has been set to 31.");
    }

    if (expire_tiles_zoom != 0 && projection->target_srs() != 3857) {
        log_warn("Expire has been enabled (with -e or --expire-tiles) but "
                 "target SRS is not Mercator (EPSG:3857). Expire disabled!");
        expire_tiles_zoom = 0;
    }

    if (output_backend == "flex" || output_backend == "gazetteer") {
        if (style == DEFAULT_STYLE) {
            throw std::runtime_error{
                "You have to set the config file with the -S|--style option."};
        }
    }

    if (output_backend == "gazetteer") {
        log_warn(
            "The 'gazetteer' output is deprecated and will soon be removed.");
    }
}
