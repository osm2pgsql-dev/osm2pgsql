/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2025 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "command-line-parser.hpp"

#include "command-line-app.hpp"
#include "format.hpp"
#include "logging.hpp"
#include "options.hpp"
#include "pgsql.hpp"
#include "projection.hpp"
#include "reprojection.hpp"
#include "util.hpp"
#include "version.hpp"

#include <osmium/util/string.hpp>
#include <osmium/version.hpp>

#include <CLI/CLI.hpp>

#include <lua.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <thread> // for number of threads
#include <vector>

namespace {

void error_bbox()
{
    throw std::runtime_error{"Bounding box must be specified like: "
                             "minlon,minlat,maxlon,maxlat."};
}

double parse_and_check_coordinate(std::string const &str)
{
    char *end = nullptr;

    double const value = std::strtod(str.c_str(), &end);
    if (end != &*str.end() || !std::isfinite(value)) {
        error_bbox();
    }

    return value;
}

osmium::Box parse_bbox_param(std::string const &arg)
{
    auto const values = osmium::split_string(arg, ',', true);
    if (values.size() != 4) {
        error_bbox();
    }

    double const minx = parse_and_check_coordinate(values[0]);
    double const miny = parse_and_check_coordinate(values[1]);
    double const maxx = parse_and_check_coordinate(values[2]);
    double const maxy = parse_and_check_coordinate(values[3]);

    if (maxx <= minx) {
        throw std::runtime_error{
            "Bounding box failed due to maxlon <= minlon."};
    }

    if (maxy <= miny) {
        throw std::runtime_error{
            "Bounding box failed due to maxlat <= minlat."};
    }

    log_debug("Applying bounding box: {},{} to {},{}", minx, miny, maxx, maxy);

    osmium::Box const box{minx, miny, maxx, maxy};
    if (!box.valid()) {
        error_bbox();
    }

    return box;
}

void parse_expire_tiles_param(char const *arg, uint32_t *expire_tiles_zoom_min,
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

void check_options_non_slim(CLI::App const &app)
{
    std::vector<std::string> const slim_options = {
        "--cache", "--middle-schema", "--middle-with-nodes",
        "--tablespace-slim-data", "--tablespace-slim-index"};

    for (auto const &opt : slim_options) {
        if (app.count(opt) > 0) {
            log_warn("Ignoring option {}. Can only be used in --slim mode.",
                     app.get_option(opt)->get_name(false, true));
        }
    }
}

void check_options_output_flex(CLI::App const &app)
{
    auto const ignored_options = app.get_options([](CLI::Option const *option) {
        return option->get_group() == "Pgsql output options" ||
               option->get_name() == "--tablespace-main-data" ||
               option->get_name() == "--tablespace-main-index";
    });

    for (auto const *opt : ignored_options) {
        if (opt->count()) {
            log_warn("Ignoring option {} for 'flex' output",
                     opt->get_name(false, true));
        }
    }
}

void check_options_output_null(CLI::App const &app)
{
    auto const ignored_options = app.get_options([](CLI::Option const *option) {
        return option->get_group() == "Pgsql output options" ||
               option->get_group() == "Expire options" ||
               option->get_name() == "--style" ||
               option->get_name() == "--disable-parallel-indexing" ||
               option->get_name() == "--number-processes";
    });

    for (auto const *opt : ignored_options) {
        if (opt->count()) {
            log_warn("Ignoring option {} for 'null' output",
                     opt->get_name(false, true));
        }
    }
}

void check_options_output_pgsql(CLI::App const &app, options_t *options)
{
    if (app.count("--latlong") + app.count("--merc") + app.count("--proj") >
        1) {
        throw std::runtime_error{"You can only use one of --latlong, -l, "
                                 "--merc, -m, --proj, and -E"};
    }

    if (options->hstore_mode == hstore_column::none &&
        options->hstore_columns.empty() && options->hstore_match_only) {
        log_warn("--hstore-match-only only makes sense with --hstore, "
                 "--hstore-all, or --hstore-column; ignored.");
        options->hstore_match_only = false;
    }

    if (options->enable_hstore_index &&
        options->hstore_mode == hstore_column::none &&
        options->hstore_columns.empty()) {
        log_warn("--hstore-add-index only makes sense with hstore enabled; "
                 "ignored.");
        options->enable_hstore_index = false;
    }
}

void check_options(options_t *options)
{
    if (options->append && !options->slim) {
        throw std::runtime_error{"--append can only be used with slim mode!"};
    }

    if (options->cache < 0) {
        options->cache = 0;
        log_warn("RAM cache cannot be negative. Using 0 instead.");
    }

    if (options->cache == 0) {
        if (!options->slim) {
            throw std::runtime_error{
                "RAM node cache can only be disabled in slim mode."};
        }
        if (options->flat_node_file.empty() && !options->append) {
            log_warn("RAM cache is disabled. This will likely slow down "
                     "processing a lot.");
        }
    }
}

void check_options_expire(options_t *options) {
    // Zoom level 31 is the technical limit because we use 32-bit integers for
    // the x and y index of a tile ID.
    if (options->expire_tiles_zoom_min > 31) {
        options->expire_tiles_zoom_min = 31;
        log_warn("Minimum zoom level for tile expiry is too "
                 "large and has been set to 31.");
    }

    if (options->expire_tiles_zoom > 31) {
        options->expire_tiles_zoom = 31;
        log_warn("Maximum zoom level for tile expiry is too "
                 "large and has been set to 31.");
    }

    if (options->expire_tiles_zoom != 0 &&
        options->projection->target_srs() != PROJ_SPHERE_MERC) {
        log_warn("Expire has been enabled (with -e or --expire-tiles) but "
                 "target SRS is not Mercator (EPSG:3857). Expire disabled!");
        options->expire_tiles_zoom = 0;
    }
}

} // anonymous namespace

void print_version()
{
    fmt::print(stderr, "osm2pgsql version {}\n", get_osm2pgsql_version());
    fmt::print(stderr, "Build: {}\n", get_build_type());
    fmt::print(stderr, "Compiled using the following library versions:\n");
    fmt::print(stderr, "Libosmium {}\n", LIBOSMIUM_VERSION_STRING);
    fmt::print(stderr, "Proj {}\n", get_proj_version());
#ifdef HAVE_LUAJIT
    fmt::print(stderr, "{} ({})\n", LUA_RELEASE, LUAJIT_VERSION);
#else
    fmt::print(stderr, "{}\n", LUA_RELEASE);
#endif
}

// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,hicpp-avoid-c-arrays,modernize-avoid-c-arrays)
options_t parse_command_line(int argc, char *argv[])
{
    options_t options;

    options.num_procs = std::min(4U, std::thread::hardware_concurrency());
    if (options.num_procs < 1) {
        log_warn("Unable to detect number of hardware threads supported!"
                 " Using single thread.");
        options.num_procs = 1;
    }

    command_line_app_t app{"osm2pgsql -- Import OpenStreetMap data into a "
                           "PostgreSQL/PostGIS database\n"};
    app.get_formatter()->column_width(38);

    app.add_option("OSMFILE", options.input_files)
        ->description(
            "OSM input file(s). Read manual before using multiple files!")
        ->type_name("");

    // ----------------------------------------------------------------------
    // Main options
    // ----------------------------------------------------------------------

    // --append
    app.add_flag("-a,--append", options.append)
        ->description("Update existing osm2pgsql database (needs --slim).");

    // --create
    app.add_flag("-c,--create")
        ->description("Import OSM data from file into database. This is the "
                      "default if --append is not used.");

    // --slim
    app.add_flag("-s,--slim", options.slim)
        ->description("Store raw OSM data in the database."
                      " Required if you want to update with --append later.");

    // ----------------------------------------------------------------------
    // Database options
    // ----------------------------------------------------------------------

    // --prefix
    app.add_option_function<std::string>(
           "-p,--prefix",
           [&](std::string arg) {
               options.prefix = std::move(arg);
               options.prefix_is_set = true;
               check_identifier(options.prefix, "--prefix parameter");
           })
        ->description("Prefix for table names (default: 'planet_osm').")
        ->type_name("PREFIX")
        ->group("Database options");

    // --schema
    app.add_option("--schema", options.dbschema)
        ->description("Database schema (default: 'public').")
        ->type_name("SCHEMA")
        ->group("Database options");

    // ----------------------------------------------------------------------
    // Logging options
    // ----------------------------------------------------------------------

    // --verbose
    bool verbose = false;
    app.add_flag("-v,--verbose", verbose)
        ->description("Enable debug logging.")
        ->group("Logging options");

    // ----------------------------------------------------------------------
    // Output options
    // ----------------------------------------------------------------------

    // --output
    app.add_option("-O,--output", options.output_backend)
        ->description("Set output ('pgsql' (default), 'flex', 'null').")
        ->type_name("OUTPUT")
        ->group("Output options");

    // --style
    app.add_option("-S,--style", options.style)
        ->description("Location of the style file. (Default: '" DEFAULT_STYLE
                      "').")
        ->type_name("FILE")
        ->check(CLI::ExistingFile)
        ->group("Output options");

    // ----------------------------------------------------------------------
    // Pgsql output options
    // ----------------------------------------------------------------------

    // --hstore
    auto *opt_hstore =
        app.add_flag_function(
               "-k,--hstore",
               [&](int64_t) { options.hstore_mode = hstore_column::norm; })
            ->description("Add tags without column to an additional hstore "
                          "(key/value) column.")
            ->group("Pgsql output options");

    // --hstore-add-index
    app.add_flag("--hstore-add-index", options.enable_hstore_index)
        ->description("Add index to hstore (key/value) column.")
        ->group("Pgsql output options");

    // --hstore-all
    auto *const opt_hstore_all =
        app.add_flag_function(
               "-j,--hstore-all",
               [&](int64_t) { options.hstore_mode = hstore_column::all; })
            ->description(
                "Add all tags to an additional hstore (key/value) column.")
            ->group("Pgsql output options")
            ->excludes(opt_hstore);

    opt_hstore->excludes(opt_hstore_all);

    // --hstore-column
    app.add_option("-z,--hstore-column", options.hstore_columns)
        ->description("Add additional hstore (key/value) column.")
        ->type_name("NAME")
        ->group("Pgsql output options");

    // --hstore-match-only
    app.add_flag("--hstore-match-only", options.hstore_match_only)
        ->description("Only keep objects that have a non-NULL value in one of "
                      "the columns.")
        ->group("Pgsql output options");

    // --keep-coastlines
    app.add_flag("-K,--keep-coastlines", options.keep_coastlines)
        ->description("Keep coastline data (default: discard objects tagged"
                      " natural=coastline).")
        ->group("Pgsql output options");

    // --latlong
    app.add_flag_function("-l,--latlong",
                          [&](int64_t) {
                              options.projection =
                                  reprojection::create_projection(PROJ_LATLONG);
                          })
        ->description("Store data in degrees of latitude & longitude (WGS84).")
        ->group("Pgsql output options");

    // --merc
    app.add_flag_function("-m,--merc",
                          [&](int64_t) {
                              options.projection =
                                  reprojection::create_projection(
                                      PROJ_SPHERE_MERC);
                          })
        ->description("Store data in Web Mercator [EPSG 3857]. This is the "
                      "default if --latlong or --proj are not used.")
        ->group("Pgsql output options");

    // --multi-geometry
    app.add_flag("-G,--multi-geometry", options.enable_multi)
        ->description("Generate multi-geometry features in database tables.")
        ->group("Pgsql output options");

    // --output-pgsql-schema
    app.add_option("--output-pgsql-schema", options.output_dbschema)
        ->description("Database schema for pgsql output tables"
                      " (default: setting of --schema).")
        ->type_name("SCHEMA")
        ->group("Pgsql output options");

    // --proj
    app.add_option_function<int>("-E,--proj",
#ifdef HAVE_GENERIC_PROJ
                                 [&](int arg) {
                                     options.projection =
                                         reprojection::create_projection(arg);
#else
           [&](int) {
               throw std::runtime_error{
                   "Generic projections not available in this build."};
#endif
                                 })
#ifdef HAVE_GENERIC_PROJ
        ->description("Use projection EPSG:SRID.")
#else
        ->description("Use projection EPSG:SRID (not available in this build).")
#endif
        ->type_name("SRID")
        ->group("Pgsql output options");

    // --reproject-area
    app.add_flag("--reproject-area", options.reproject_area)
        ->description("Compute area column using Web Mercator coordinates.")
        ->group("Pgsql output options");

    // --tag-transform-script
    app.add_option("--tag-transform-script", options.tag_transform_script)
        ->description(
            "Specify a Lua script to handle tag filtering and normalisation.")
        ->option_text("SCRIPT")
        ->check(CLI::ExistingFile)
        ->group("Pgsql output options");

    // ----------------------------------------------------------------------
    // Expire options
    // ----------------------------------------------------------------------

    // --expire-bbox-size
    app.add_option("--expire-bbox-size", options.expire_tiles_max_bbox)
        ->description("Max size for a polygon to expire the whole polygon, not "
                      "just the boundary (default: 20000).")
        ->type_name("SIZE")
        ->group("Expire options");

    // --expire-output
    app.add_option("-o,--expire-output", options.expire_tiles_filename)
        ->description("Output filename for expired tiles list.")
        ->type_name("FILE")
        ->group("Expire options");

    // --expire-tiles
    app.add_option_function<std::string>("-e,--expire-tiles",
                                         [&](std::string const &arg) {
                                             parse_expire_tiles_param(
                                                 arg.c_str(),
                                                 &options.expire_tiles_zoom_min,
                                                 &options.expire_tiles_zoom);
                                         })
        ->description("Create a tile expiry list. Zoom levels must be larger "
                      "than 0 and smaller than 32.")
        ->type_name("[MINZOOM-]MAXZOOM")
        ->group("Expire options");

    // ----------------------------------------------------------------------
    // Middle options
    // ----------------------------------------------------------------------

    // --cache
    app.add_option("-C,--cache", options.cache)
        ->description("Use up to SIZE MB for caching nodes (default: 800).")
        ->type_name("SIZE")
        ->group("Middle options");

    // --drop
    app.add_flag("--drop", options.droptemp)
        ->description("Drop middle tables and flat node file after import.")
        ->group("Middle options");

    // --extra-attributes
    app.add_flag("-x,--extra-attributes", options.extra_attributes)
        ->description("Include attributes (version, timestamp, changeset id,"
                      " user id, and user name) for each OSM object.")
        ->group("Middle options");

    // --flat-nodes
    app.add_option("-F,--flat-nodes", options.flat_node_file)
        ->description(
            "File for storing node locations (default: store in database).")
        ->type_name("FILE")
        ->group("Middle options");

    // --middle-schema
    app.add_option("--middle-schema", options.middle_dbschema)
        ->description(
            "Database schema for middle tables (default: setting of --schema).")
        ->type_name("SCHEMA")
        ->group("Middle options");

    // --middle-with-nodes
    app.add_flag("--middle-with-nodes", options.middle_with_nodes)
        ->description("Store tagged nodes in db (new middle db format only).")
        ->group("Middle options");

    // ----------------------------------------------------------------------
    // Input options
    // ----------------------------------------------------------------------

    // --bbox
    app.add_option_function<std::string>("-b,--bbox",
                                         [&](std::string const &arg) {
                                             options.bbox =
                                                 parse_bbox_param(arg);
                                         })
        ->description("Apply a bounding box filter on the imported data, e.g. "
                      "'--bbox -0.5,51.25,0.5,51.75'.")
        ->type_name("MINX,MINY,MAXX,MAXY")
        ->group("Input options");

    // --input-reader
    app.add_option("-r,--input-reader", options.input_format)
        ->description("Input format ('xml', 'pbf', 'o5m', 'opl',"
                      " 'auto' - autodetect format (default)).")
        ->type_name("FORMAT")
        ->group("Input options");

    // ----------------------------------------------------------------------
    // Advanced options
    // ----------------------------------------------------------------------

    // --disable-parallel-indexing
    app.add_flag_function("-I,--disable-parallel-indexing",
                          [&](int64_t) { options.parallel_indexing = false; })
        ->description("Disable concurrent index creation.")
        ->group("Advanced options");

    // --number-processes
    app.add_option("--number-processes", options.num_procs)
        // The threads will open up database connections which will
        // run out at some point. It depends on the number of tables
        // how many connections there are. The number 32 is way beyond
        // anything that will make sense here.
        ->transform(CLI::Bound(1, 32))
        ->description("Specifies the number of parallel processes used for "
                      "certain operations (default: number of CPUs).")
        ->type_name("NUM")
        ->group("Advanced options");

    // ----------------------------------------------------------------------
    // Tablespace options
    // ----------------------------------------------------------------------

    app.add_option("--tablespace-main-data", options.tblsmain_data)
        ->description("Tablespace for main tables.")
        ->option_text("TBLSPC")
        ->group("Tablespace options");

    app.add_option("--tablespace-main-index", options.tblsmain_index)
        ->description("Tablespace for main indexes.")
        ->option_text("TBLSPC")
        ->group("Tablespace options");

    app.add_option("--tablespace-slim-data", options.tblsslim_data)
        ->description("Tablespace for slim mode tables.")
        ->option_text("TBLSPC")
        ->group("Tablespace options");

    app.add_option("--tablespace-slim-index", options.tblsslim_index)
        ->description("Tablespace for slim mode indexes.")
        ->option_text("TBLSPC")
        ->group("Tablespace options");

    try {
        app.parse(argc, argv);
    } catch (...) {
        log_info("osm2pgsql version {}", get_osm2pgsql_version());
        throw;
    }

    if (app.want_help()) {
        std::cout << app.help();
        options.command = command_t::help;
        return options;
    }

    if (app.want_version()) {
        options.command = command_t::version;
        return options;
    }

    log_info("osm2pgsql version {}", get_osm2pgsql_version());

    if (verbose) {
        get_logger().set_level(log_level::debug);
    }

    if (options.append && app.count("--create")) {
        throw std::runtime_error{"--append and --create options can not be "
                                 "used at the same time!"};
    }

    check_options(&options);

    if (options.slim) { // slim mode, use database middle
        options.middle_database_format = 2;
    } else { // non-slim mode, use ram middle
        check_options_non_slim(app);
    }

    if (options.output_backend == "flex") {
        check_options_output_flex(app);
    } else if (options.output_backend == "null") {
        check_options_output_null(app);
    } else if (options.output_backend == "pgsql" ||
               options.output_backend.empty()) {
        check_options_output_pgsql(app, &options);
    }

    if (options.input_format == "auto") {
        options.input_format.clear();
    }

    if (options.dbschema.empty()) {
        throw std::runtime_error{"Schema can not be empty."};
    }
    check_identifier(options.dbschema, "--schema parameter");

    if (options.middle_dbschema.empty()) {
        options.middle_dbschema = options.dbschema;
    } else {
        check_identifier(options.middle_dbschema, "--middle-schema parameter");
    }

    if (options.output_dbschema.empty()) {
        options.output_dbschema = options.dbschema;
    } else {
        check_identifier(options.output_dbschema,
                         "--output-pgsql-schema parameter");
    }

    if (options.input_files.empty()) {
        throw std::runtime_error{
            "Missing input file(s). Try 'osm2pgsql --help'."};
    }

    if (!options.projection) {
        options.projection = reprojection::create_projection(PROJ_SPHERE_MERC);
    }

    check_options_expire(&options);

    options.connection_params = app.connection_params();

    return options;
}
