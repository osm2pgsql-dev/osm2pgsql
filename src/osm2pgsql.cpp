/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2024 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "command-line-parser.hpp"
#include "input.hpp"
#include "logging.hpp"
#include "middle.hpp"
#include "options.hpp"
#include "osmdata.hpp"
#include "output.hpp"
#include "pgsql.hpp"
#include "pgsql-capabilities.hpp"
#include "properties.hpp"
#include "util.hpp"
#include "version.hpp"

#include <osmium/util/memory.hpp>

#include <exception>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>

namespace {

/**
 * Output overall memory usage as debug message.
 *
 * This only works on Linux.
 */
void show_memory_usage()
{
    osmium::MemoryUsage const mem;
    if (mem.peak() != 0) {
        log_debug("Overall memory usage: peak={}MByte current={}MByte",
                  mem.peak(), mem.current());
    }
}

file_info run(options_t const &options, properties_t *properties)
{
    auto const files = prepare_input_files(
        options.input_files, options.input_format, options.append);

    auto thread_pool = std::make_shared<thread_pool_t>(
        options.parallel_indexing ? options.num_procs : 1U);
    log_debug("Started pool with {} threads.", thread_pool->num_threads());

    auto middle = create_middle(thread_pool, options);
    middle->start();

    auto output = output_t::create_output(middle->get_query_instance(),
                                          thread_pool, options, *properties);

    middle->set_requirements(output->get_requirements());

    if (!options.append) {
        properties->init_table();
    }
    properties->store();

    osmdata_t osmdata{middle, output, options};

    // Processing: In this phase the input file(s) are read and parsed,
    // populating some of the tables.
    auto finfo = process_files(files, &osmdata, options.append,
                               get_logger().show_progress());

    show_memory_usage();

    // Process pending ways and relations. Cluster database tables and
    // create indexes.
    osmdata.stop();

    return finfo;
}

void check_db(options_t const &options)
{
    pg_conn_t const db_connection{options.connection_params, "check"};

    init_database_capabilities(db_connection);

    auto const pv = get_postgis_version();
    if (pv.major < 2 || (pv.major == 2 && pv.minor < 5)) {
        throw std::runtime_error{"Need at least PostGIS version 2.5"};
    }

    check_schema(options.dbschema);
    check_schema(options.middle_dbschema);
    check_schema(options.output_dbschema);
}

// This is called in "create" mode to initialize properties.
void set_up_properties(properties_t *properties, options_t const &options)
{
    properties->set_bool("attributes", options.extra_attributes);

    if (options.flat_node_file.empty()) {
        properties->set_string("flat_node_file", "");
    } else {
        properties->set_string(
            "flat_node_file", std::filesystem::absolute(
                                  std::filesystem::path{options.flat_node_file})
                                  .string());
    }

    properties->set_string("prefix", options.prefix);
    properties->set_bool("updatable", options.slim && !options.droptemp);
    properties->set_string("version", get_osm2pgsql_short_version());
    properties->set_int("db_format", options.middle_database_format);
    properties->set_string("output", options.output_backend);

    if (options.style.empty()) {
        properties->set_string("style", "");
    } else {
        properties->set_string(
            "style",
            std::filesystem::absolute(std::filesystem::path{options.style})
                .string());
    }
}

void store_data_properties(properties_t *properties, file_info const &finfo)
{
    if (finfo.last_timestamp.valid()) {
        auto const timestamp = finfo.last_timestamp.to_iso();
        properties->set_string("import_timestamp", timestamp);
        properties->set_string("current_timestamp", timestamp);
    }

    for (std::string const s : {"base_url", "sequence_number", "timestamp"}) {
        auto const value = finfo.header.get("osmosis_replication_" + s);
        if (!value.empty()) {
            properties->set_string("replication_" + s, value);
        }
    }
}

void check_updatable(properties_t const &properties)
{
    if (properties.get_bool("updatable", false)) {
        return;
    }

    throw std::runtime_error{
        "This database is not updatable. To create an"
        " updatable database use --slim (without --drop)."};
}

void check_attributes(properties_t const &properties, options_t *options)
{
    bool const with_attributes = properties.get_bool("attributes", false);

    if (options->extra_attributes) {
        if (!with_attributes) {
            throw std::runtime_error{
                "Can not update with attributes (-x/--extra-attributes)"
                " because original import was without attributes."};
        }
        return;
    }

    if (with_attributes) {
        log_info("Updating with attributes (same as on import).");
        options->extra_attributes = true;
    }
}

void check_and_update_flat_node_file(properties_t *properties,
                                     options_t *options)
{
    auto const flat_node_file_from_import =
        properties->get_string("flat_node_file", "");
    if (options->flat_node_file.empty()) {
        if (flat_node_file_from_import.empty()) {
            log_info("Not using flat node file (same as on import).");
        } else {
            options->flat_node_file = flat_node_file_from_import;
            log_info("Using flat node file '{}' (same as on import).",
                     flat_node_file_from_import);
        }
    } else {
        const auto absolute_path =
            std::filesystem::absolute(
                std::filesystem::path{options->flat_node_file})
                .string();

        if (flat_node_file_from_import.empty()) {
            throw fmt_error("Database was imported without flat node file. Can"
                            " not use flat node file '{}' now.",
                            options->flat_node_file);
        }

        if (absolute_path == flat_node_file_from_import) {
            log_info("Using flat node file '{}' (same as on import).",
                     flat_node_file_from_import);
        } else {
            log_info(
                "Using the flat node file you specified on the command line"
                " ('{}') instead of the one used on import ('{}').",
                absolute_path, flat_node_file_from_import);
            properties->set_string("flat_node_file", absolute_path);
        }
    }
}

void check_prefix(properties_t const &properties, options_t *options)
{
    auto const prefix = properties.get_string("prefix", "planet_osm");
    if (!options->prefix_is_set) {
        log_info("Using prefix '{}' (same as on import).", prefix);
        options->prefix = prefix;
        return;
    }

    if (prefix != options->prefix) {
        throw fmt_error("Different prefix specified on command line ('{}')"
                        " then used on import ('{}').",
                        options->prefix, prefix);
    }
}

void check_db_format(properties_t const &properties, options_t *options)
{
    auto const format = properties.get_int("db_format", -1);

    if (format == 1) {
        throw std::runtime_error{
            "Old database format detected. This version of osm2pgsql can not "
            "read this any more. Downgrade osm2pgsql or reimport database."};
    }

    if (format != 2) {
        throw fmt_error("Unknown db_format '{}' in properties.", format);
    }

    options->middle_database_format = static_cast<uint8_t>(format);
}

void check_output(properties_t const &properties, options_t *options)
{
    auto const output = properties.get_string("output", "pgsql");

    if (options->output_backend.empty()) {
        options->output_backend = output;
        log_info("Using output '{}' (same as on import).", output);
        return;
    }

    if (options->output_backend == output) {
        return;
    }

    throw fmt_error("Different output specified on command line ('{}')"
                    " then used on import ('{}').",
                    options->output_backend, output);
}

void check_and_update_style_file(properties_t *properties, options_t *options)
{
    auto const style_file_from_import = properties->get_string("style", "");

    if (options->style.empty()) {
        log_info("Using style file '{}' (same as on import).",
                 style_file_from_import);
        options->style = style_file_from_import;
        return;
    }

    if (style_file_from_import.empty()) {
        throw std::runtime_error{"Style file from import is empty!?"};
    }

    const auto absolute_path =
        std::filesystem::absolute(std::filesystem::path{options->style})
            .string();

    if (absolute_path == style_file_from_import) {
        log_info("Using style file '{}' (same as on import).",
                 style_file_from_import);
        return;
    }

    log_info("Using the style file you specified on the command line"
             " ('{}') instead of the one used on import ('{}').",
             absolute_path, style_file_from_import);
    properties->set_string("style", absolute_path);
}

// This is called in "append" mode to check that the command line options are
// compatible with the properties stored in the database.
void check_and_update_properties(properties_t *properties, options_t *options)
{
    check_updatable(*properties);
    check_attributes(*properties, options);
    check_and_update_flat_node_file(properties, options);
    check_prefix(*properties, options);
    check_db_format(*properties, options);
    check_output(*properties, options);
    check_and_update_style_file(properties, options);
}

void set_option_defaults(options_t *options)
{
    if (options->output_backend.empty()) {
        options->output_backend = "pgsql";
    }

    if (options->style.empty()) {
        if (options->output_backend == "flex") {
            throw std::runtime_error{"You have to set the config file "
                                     "with the -S|--style option."};
        }
        if (options->output_backend == "pgsql") {
            options->style = DEFAULT_STYLE;
        }
    }
}

} // anonymous namespace

// NOLINTNEXTLINE(bugprone-exception-escape)
int main(int argc, char *argv[])
{
    try {
        auto options = parse_command_line(argc, argv);

        if (options.command == command_t::help) {
            // Already handled inside parse_command_line()
            return 0;
        }

        if (options.command == command_t::version) {
            print_version();
            return 0;
        }

        util::timer_t timer_overall;

        check_db(options);

        properties_t properties{options.connection_params,
                                options.middle_dbschema};

        if (options.append) {
            if (!properties.load()) {
                throw std::runtime_error{
                    "Did not find table 'osm2pgsql_properties' in database. "
                    "Database too old? Wrong schema?"};
            }

            check_and_update_properties(&properties, &options);
            properties.store();

            auto const finfo = run(options, &properties);

            if (finfo.last_timestamp.valid()) {
                auto const current_timestamp =
                    properties.get_string("current_timestamp", "");

                if (current_timestamp.empty() ||
                    (finfo.last_timestamp >
                     osmium::Timestamp{current_timestamp})) {
                    properties.set_string("current_timestamp",
                                          finfo.last_timestamp.to_iso());
                }
            }
        } else {
            set_option_defaults(&options);
            set_up_properties(&properties, options);
            auto const finfo = run(options, &properties);
            store_data_properties(&properties, finfo);
        }

        properties.store();

        show_memory_usage();
        log_info("osm2pgsql took {} overall.",
                 util::human_readable_duration(timer_overall.stop()));
    } catch (std::exception const &e) {
        log_error("{}", e.what());
        return 1;
    } catch (...) {
        log_error("Unknown exception.");
        return 1;
    }

    return 0;
}
