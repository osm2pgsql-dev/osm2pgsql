/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "dependency-manager.hpp"
#include "input.hpp"
#include "logging.hpp"
#include "middle.hpp"
#include "options.hpp"
#include "osmdata.hpp"
#include "output.hpp"
#include "pgsql.hpp"
#include "pgsql-capabilities.hpp"
#include "pgsql-helper.hpp"
#include "settings.hpp"
#include "util.hpp"
#include "version.hpp"

#include <osmium/util/memory.hpp>

#include <boost/filesystem.hpp>

#include <exception>
#include <memory>
#include <utility>

/**
 * Output overall memory usage as debug message.
 *
 * This only works on Linux.
 */
static void show_memory_usage()
{
    osmium::MemoryUsage const mem;
    if (mem.peak() != 0) {
        log_debug("Overall memory usage: peak={}MByte current={}MByte",
                  mem.peak(), mem.current());
    }
}

static void run(options_t const &options)
{
    auto const files = prepare_input_files(
        options.input_files, options.input_format, options.append);

    auto thread_pool = std::make_shared<thread_pool_t>(
        options.parallel_indexing ? options.num_procs : 1U);
    log_debug("Started pool with {} threads.", thread_pool->num_threads());

    auto middle = create_middle(thread_pool, options);
    middle->start();

    auto output = output_t::create_output(middle->get_query_instance(),
                                          thread_pool, options);

    middle->set_requirements(output->get_requirements());

    auto dependency_manager =
        options.with_forward_dependencies
            ? std::make_unique<full_dependency_manager_t>(middle)
            : std::make_unique<dependency_manager_t>();

    osmdata_t osmdata{std::move(dependency_manager), middle, output, options};

    osmdata.start();

    // Processing: In this phase the input file(s) are read and parsed,
    // populating some of the tables.
    process_files(files, &osmdata, options.append,
                  get_logger().show_progress());

    show_memory_usage();

    // Process pending ways and relations. Cluster database tables and
    // create indexes.
    osmdata.stop();
}

static void check_db(options_t const &options)
{
    pg_conn_t const db_connection{options.conninfo};

    init_database_capabilities(db_connection);

    check_schema(options.middle_dbschema);
    check_schema(options.output_dbschema);
}

// This is called in "create" mode to store settings into the database
static void store_settings(options_t const &options)
{
    settings_t settings{options.conninfo, options.middle_dbschema};

    settings.set_bool("attributes", options.extra_attributes);

    if (options.flat_node_file.empty()) {
        settings.set_string("flat-nodes", "");
    } else {
        settings.set_string("flat-nodes",
                            boost::filesystem::absolute(
                                boost::filesystem::path{options.flat_node_file})
                                .string());
    }

    settings.set_string("prefix", options.prefix);
    settings.set_bool("updatable", options.slim && !options.droptemp);
    settings.set_string("version", get_osm2pgsql_short_version());

    settings.store();
}

// This is called in "append" mode to check that the command line options
// are compatible with the settings stored in the database. For legacy systems
// that haven't stored the settings in the database, it will return false.
static bool check_and_update_options(options_t *options)
{
    settings_t settings{options->conninfo, options->middle_dbschema};
    if (!settings.load()) {
        return false;
    }

    if (!settings.get_bool("updatable", false)) {
        throw std::runtime_error{
            "This database is not updatable. To create an"
            " updatable database use --slim (without --drop)."};
    }

    bool const with_attributes = settings.get_bool("attributes", false);
    if (options->extra_attributes) {
        if (!with_attributes) {
            throw std::runtime_error{
                "Can not update with attributes (-x/--extra-attributes) because"
                " original import was without attributes."};
        }
    } else if (with_attributes) {
        log_info("Updating with attributes (same as on import).");
    }

    auto const flat_node_file_from_import =
        settings.get_string("flat-nodes", "");
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
            boost::filesystem::absolute(
                boost::filesystem::path{options->flat_node_file})
                .string();

        if (flat_node_file_from_import.empty()) {
            throw fmt_error("Database was imported without flat node file. Can"
                            " not use flat node file '{}' now.",
                            options->flat_node_file);
        } else if (absolute_path == flat_node_file_from_import) {
            log_info("Using flat node file '{}' (same as on import).",
                     flat_node_file_from_import);
        } else {
            log_info(
                "Using the flat node file you specified on the command line"
                " ('{}') instead of the one used on import ('{}').",
                absolute_path, flat_node_file_from_import);
            settings.set_string("flat-nodes", absolute_path, true);
        }
    }

    auto const prefix = settings.get_string("prefix", "planet_osm");
    if (!options->prefix_is_set) {
        log_info("Using prefix '{}' (same as on import).", prefix);
        options->prefix = prefix;
    } else if (prefix != options->prefix) {
        throw fmt_error("Different prefix specified on command line ('{}')"
                        " then used on import ('{}').",
                        options->prefix, prefix);
    }

    return true;
}

// If we are in append mode and the middle nodes table isn't there, it probably
// means we used a flat node store when we created this database. Check for
// that and stop if it looks like we are missing the node location store
// option. (This function is only used in legacy systems which don't have the
// settings stored in the database.)
static void check_for_nodes_table(options_t const &options)
{
    if (!options.flat_node_file.empty()) {
        return;
    }

    if (!has_table(options.middle_dbschema.empty() ? "public"
                                                   : options.middle_dbschema,
                   options.prefix + "_nodes")) {
        throw std::runtime_error{"You seem to not have a nodes table. Did "
                                 "you forget the --flat-nodes option?"};
    }
}

int main(int argc, char *argv[])
{
    try {
        log_info("osm2pgsql version {}", get_osm2pgsql_version());

        options_t options{argc, argv};
        if (options.early_return()) {
            return 0;
        }

        util::timer_t timer_overall;

        check_db(options);

        if (options.append) {
            if (!check_and_update_options(&options)) {
                check_for_nodes_table(options);
            }
        } else {
            store_settings(options);
        }

        run(options);

        show_memory_usage();
        log_info("osm2pgsql took {} overall.",
                 util::human_readable_duration(timer_overall.stop()));
    } catch (std::exception const &e) {
        log_error("{}", e.what());
        return 1;
    }

    return 0;
}
