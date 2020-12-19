/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2020 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "db-check.hpp"
#include "format.hpp"
#include "input.hpp"
#include "logging.hpp"
#include "middle.hpp"
#include "options.hpp"
#include "osmdata.hpp"
#include "output.hpp"
#include "reprojection.hpp"
#include "util.hpp"
#include "version.hpp"

#include <osmium/util/memory.hpp>

#include <exception>
#include <memory>

static void run(options_t const &options)
{
    auto const files = prepare_input_files(
        options.input_files, options.input_format, options.append);

    auto middle = create_middle(options);
    middle->start();

    auto const outputs =
        output_t::create_outputs(middle->get_query_instance(), options);

    auto dependency_manager = std::unique_ptr<dependency_manager_t>(
        options.with_forward_dependencies
            ? new full_dependency_manager_t{middle}
            : new dependency_manager_t{});

    osmdata_t osmdata{std::move(dependency_manager), middle, outputs,
                        options};

    osmdata.start();

    // Processing: In this phase the input file(s) are read and parsed,
    // populating some of the tables.
    process_files(files, osmdata, options.append,
                    get_logger().show_progress());

    // Process pending ways and relations. Cluster database tables and
    // create indexes.
    osmdata.stop();
}

int main(int argc, char *argv[])
{
    try {
        log_info("osm2pgsql version {}", get_osm2pgsql_version());

        options_t const options{argc, argv};
        if (options.long_usage_bool) {
            return 0;
        }

        util::timer_t timer_overall;

        check_db(options);

        run(options);

        // Output overall memory usage. This only works on Linux.
        osmium::MemoryUsage mem;
        if (mem.peak() != 0) {
            log_debug("Overall memory usage: peak={}MByte current={}MByte",
                      mem.peak(), mem.current());
        }

        log_info("osm2pgsql took {} overall.",
                 util::human_readable_duration(timer_overall.stop()));
    } catch (std::exception const &e) {
        log_error("{}", e.what());
        return 1;
    }

    return 0;
}
