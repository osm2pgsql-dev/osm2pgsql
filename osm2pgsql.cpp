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

#include "config.h"
#include "osmtypes.hpp"
#include "reprojection.hpp"
#include "options.hpp"
#include "parse.hpp"
#include "middle.hpp"
#include "output.hpp"
#include "osmdata.hpp"
#include "util.hpp"

#include <time.h>
#include <stdexcept>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>

#include <libpq-fe.h>
#include <boost/format.hpp>

void check_db(const char* conninfo, const int unlogged)
{
    PGconn *sql_conn = PQconnectdb(conninfo);

    //make sure you can connect
    if (PQstatus(sql_conn) != CONNECTION_OK) {
        throw std::runtime_error((boost::format("Error: Connection to database failed: %1%\n") % PQerrorMessage(sql_conn)).str());
    }

    //make sure unlogged it is supported by your database if you want it
    if (unlogged && PQserverVersion(sql_conn) < 90100) {
        throw std::runtime_error((
            boost::format("Error: --unlogged works only with PostgreSQL 9.1 and above, but\n you are using PostgreSQL %1%.%2%.%3%.\n")
            % (PQserverVersion(sql_conn) / 10000)
            % ((PQserverVersion(sql_conn) / 100) % 100)
            % (PQserverVersion(sql_conn) % 100)).str());
    }

    PQfinish(sql_conn);
}

int main(int argc, char *argv[])
{
    fprintf(stderr, "osm2pgsql SVN version %s (%lubit id space)\n\n", VERSION, 8 * sizeof(osmid_t));
    try
    {
        //parse the args into the different options members
        options_t options = options_t::parse(argc, argv);
        if(options.long_usage_bool)
            return 0;

        //setup the front (input)
        parse_delegate_t parser(options.extra_attributes, options.bbox, options.projection);

        //setup the middle
        boost::shared_ptr<middle_t> middle = middle_t::create_middle(options.slim);

        //setup the backend (output)
        std::vector<boost::shared_ptr<output_t> > outputs = output_t::create_outputs(middle.get(), options);

        //let osmdata orchestrate between the middle and the outs
        osmdata_t osmdata(middle, outputs);

        //check the database
        check_db(options.conninfo.c_str(), options.unlogged);

        fprintf(stderr, "Using projection SRS %d (%s)\n",
                options.projection->project_getprojinfo()->srs,
                options.projection->project_getprojinfo()->descr );

        //start it up
        time_t overall_start = time(NULL);
        osmdata.start();

        /* Processing
         * In this phase the input file(s) are read and parsed, populating some of the
         * tables. Not all ways can be handled before relations are processed, so they're
         * set as pending, to be handled in the next stage.
         */
        //read in the input files one by one
        for(std::vector<std::string>::const_iterator filename = options.input_files.begin(); filename != options.input_files.end(); ++filename)
        {
            //read the actual input
            fprintf(stderr, "\nReading in file: %s\n", filename->c_str());
            time_t start = time(NULL);
            if (parser.streamFile(options.input_reader.c_str(), filename->c_str(), options.sanitize, &osmdata) != 0)
                util::exit_nicely();
            fprintf(stderr, "  parse time: %ds\n", (int)(time(NULL) - start));
        }

        //show stats
        parser.printSummary();

        //Process pending ways, relations, cluster, and create indexes
        osmdata.stop();

        fprintf(stderr, "\nOsm2pgsql took %ds overall\n", (int)(time(NULL) - overall_start));

        return 0;
    }//something went wrong along the way
    catch(const std::runtime_error& e)
    {
        fprintf(stderr, "Osm2pgsql failed due to ERROR: %s\n", e.what());
        exit(EXIT_FAILURE);
    }
}
