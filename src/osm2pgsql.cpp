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

#include "format.hpp"
#include "middle-pgsql.hpp"
#include "middle-ram.hpp"
#include "options.hpp"
#include "osmdata.hpp"
#include "output.hpp"
#include "parse-osmium.hpp"
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
        fmt::print(stderr, "osm2pgsql version {}\n\n", get_osm2pgsql_version());

        options_t const options{argc, argv};
        if (options.long_usage_bool) {
            return 0;
        }

        fmt::print(stderr, "Using projection SRS {} ({})\n",
                   options.projection->target_srs(),
                   options.projection->target_desc());

        auto middle = create_middle(options);
        middle->start();

        auto const outputs =
            output_t::create_outputs(middle->get_query_instance(), options);

        bool const need_dependencies =
            std::any_of(outputs.cbegin(), outputs.cend(),
                        [](std::shared_ptr<output_t> const &output) {
                            return output->need_forward_dependencies();
                        });

        auto dependency_manager = std::unique_ptr<dependency_manager_t>(
            need_dependencies ? new full_dependency_manager_t{middle}
                              : new dependency_manager_t{});

        util::timer_t timer_overall;

        osmdata_t osmdata{std::move(dependency_manager), middle, outputs,
                          options};

        // Processing: In this phase the input file(s) are read and parsed,
        // populating some of the tables.
        parse_stats_t stats;
        for (auto const &filename : options.input_files) {
            fmt::print(stderr, "\nReading in file: {}\n", filename);
            util::timer_t timer_parse;

            parse_osmium_t parser{options.bbox, options.append, &osmdata};
            parser.stream_file(filename, options.input_reader);

            stats.update(parser.stats());

            stats.print_status(std::time(nullptr));
            fmt::print(stderr, "  parse time: {}s\n", timer_parse.stop());
        }

        stats.print_summary();

        osmdata.process();

        fmt::print(stderr, "\nOsm2pgsql took {}s overall\n",
                   timer_overall.stop());
    } catch (std::exception const &e) {
        fmt::print(stderr, "Osm2pgsql failed due to ERROR: {}\n", e.what());
        return 1;
    }
    return 0;
}
