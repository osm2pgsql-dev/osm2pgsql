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
#include "properties.hpp"
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

// This is called in "create" mode to store properties into the database.
static void store_properties(properties_t *properties, options_t const &options)
{
    properties->set_bool("attributes", options.extra_attributes);

    if (options.flat_node_file.empty()) {
        properties->set_string("flat_node_file", "");
    } else {
        properties->set_string(
            "flat_node_file",
            boost::filesystem::absolute(
                boost::filesystem::path{options.flat_node_file})
                .string());
    }

    properties->set_string("prefix", options.prefix);
    properties->set_bool("updatable", options.slim && !options.droptemp);
    properties->set_string("version", get_osm2pgsql_short_version());

    properties->store();
}

static void check_updatable(properties_t const &properties)
{
    if (properties.get_bool("updatable", false)) {
        return;
    }

    throw std::runtime_error{
        "This database is not updatable. To create an"
        " updatable database use --slim (without --drop)."};
}

static void check_attributes(properties_t const &properties, options_t *options)
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

static void check_and_update_flat_node_file(properties_t *properties,
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
            properties->set_string("flat_node_file", absolute_path, true);
        }
    }

}

static void check_prefix(properties_t const &properties, options_t *options)
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

// This is called in "append" mode to check that the command line options are
// compatible with the properties stored in the database.
static void check_and_update_properties(properties_t *properties,
                                        options_t *options)
{
    check_updatable(*properties);
    check_attributes(*properties, options);
    check_and_update_flat_node_file(properties, options);
    check_prefix(*properties, options);
}

// If we are in append mode and the middle nodes table isn't there, it probably
// means we used a flat node store when we created this database. Check for
// that and stop if it looks like we are missing the node location store
// option. (This function is only used in legacy systems which don't have the
// properties stored in the database.)
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

        properties_t properties{options.conninfo, options.middle_dbschema};
        if (options.append) {
            if (properties.load()) {
                check_and_update_properties(&properties, &options);
            } else {
                check_for_nodes_table(options);
            }
        } else {
            store_properties(&properties, options);
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
