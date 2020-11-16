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
#include "logging.hpp"
#include "middle-pgsql.hpp"
#include "middle-ram.hpp"
#include "options.hpp"
#include "osmdata.hpp"
#include "output.hpp"
#include "progress-display.hpp"
#include "reprojection.hpp"
#include "util.hpp"
#include "version.hpp"

#include <ctime>
#include <exception>
#include <memory>

static std::shared_ptr<middle_t> create_middle(options_t const &options)
{
    if (options.slim) {
        return std::make_shared<middle_pgsql_t>(&options);
    }

    return std::make_shared<middle_ram_t>(&options);
}

int main(int argc, char *argv[])
{
    try {
        log_info("osm2pgsql version {}", get_osm2pgsql_version());

        options_t const options{argc, argv};
        if (options.long_usage_bool) {
            return 0;
        }

        check_db(options);

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
        progress_display_t progress;
        for (auto const &filename : options.input_files) {
            log_info("Reading file: {}", filename);
            util::timer_t timer_parse;

            osmium::io::File file{filename, options.input_format};
            if (file.format() == osmium::io::file_format::unknown) {
                if (options.input_format.empty()) {
                    throw std::runtime_error{
                        "Cannot detect file format. Try using -r."};
                }
                throw std::runtime_error{
                    "Unknown file format '{}'."_format(options.input_format)};
            }

            progress.update(osmdata.process_file(file, options.bbox));

            if (get_logger().show_progress()) {
                progress.print_status(std::time(nullptr));
                fmt::print(stderr, "  parse time: {}\n",
                           util::human_readable_duration(timer_parse.stop()));
            }
        }

        progress.print_summary();

        // Process pending ways and relations. Cluster database tables and
        // create indexes.
        osmdata.stop();

        log_info("Osm2pgsql took {} overall",
                 util::human_readable_duration(timer_overall.stop()));
    } catch (std::exception const &e) {
        log_error("{}", e.what());
        return 1;
    }
    return 0;
}
