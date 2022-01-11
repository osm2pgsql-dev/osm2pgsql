/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "db-check.hpp"
#include "dependency-manager.hpp"
#include "input.hpp"
#include "logging.hpp"
#include "middle.hpp"
#include "options.hpp"
#include "osmdata.hpp"
#include "output.hpp"
#include "util.hpp"
#include "version.hpp"

#include <osmium/util/memory.hpp>

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
    osmium::MemoryUsage mem;
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

int main(int argc, char *argv[])
{
    try {
        log_info("osm2pgsql version {}", get_osm2pgsql_version());

        options_t const options{argc, argv};
        if (options.early_return()) {
            return 0;
        }

        util::timer_t timer_overall;

        check_db(options);

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
