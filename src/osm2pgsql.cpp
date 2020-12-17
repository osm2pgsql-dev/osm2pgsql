/*
#-----------------------------------------------------------------------------
# osm2pgsql - converts planet.osm file into PostgreSQL
# compatible output suitable to be rendered by mapnik
# Use: osm2pgsql planet.osm.bz2
#-----------------------------------------------------------------------------
# Original Python implementation by Artem Pavlenko
# Re-implementation by Jon Burgess, Copyright 2006
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
#-----------------------------------------------------------------------------
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

int main(int argc, char *argv[])
{
    try {
        log_info("osm2pgsql version {}", get_osm2pgsql_version());

        options_t const options{argc, argv};
        if (options.long_usage_bool) {
            return 0;
        }

        check_db(options);

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

        util::timer_t timer_overall;
        osmdata.start();

        // Processing: In this phase the input file(s) are read and parsed,
        // populating some of the tables.
        process_files(files, osmdata, options.append,
                      get_logger().show_progress());

        // Process pending ways and relations. Cluster database tables and
        // create indexes.
        osmdata.stop();

        // Output overall memory usage. This only works on Linux.
        osmium::MemoryUsage mem;
        if (mem.peak() != 0) {
            log_debug("Overall memory usage: peak={}MByte current={}MByte",
                      mem.peak(), mem.current());
        }

        log_info("Osm2pgsql took {} overall.",
                 util::human_readable_duration(timer_overall.stop()));
    } catch (std::exception const &e) {
        log_error("{}", e.what());
        return 1;
    }

    return 0;
}
