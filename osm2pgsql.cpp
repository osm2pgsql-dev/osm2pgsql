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
#include "options.hpp"
#include "util.hpp"
#include "text-tree.hpp"

#include <unistd.h>
#include <assert.h>
#include <time.h>
#include <stdexcept>
#include <libpq-fe.h>

int main(int argc, char *argv[])
{
    // Parse the args into the different options
	options_t options = options_t::parse(argc, argv);

    // Check the database
    PGconn *sql_conn = PQconnectdb(options.conninfo);
    if (PQstatus(sql_conn) != CONNECTION_OK) {
        fprintf(stderr, "Error: Connection to database failed: %s\n", PQerrorMessage(sql_conn));
        exit(EXIT_FAILURE);
    }
    if (options.unlogged && PQserverVersion(sql_conn) < 90100) {
        fprintf(stderr, "Error: --unlogged works only with PostgreSQL 9.1 and above, but\n");
        fprintf(stderr, "you are using PostgreSQL %d.%d.%d.\n", PQserverVersion(sql_conn) / 10000, (PQserverVersion(sql_conn) / 100) % 100, PQserverVersion(sql_conn) % 100);
        exit(EXIT_FAILURE);
    }
    PQfinish(sql_conn);

    text_init();

    //setup the front (input)
    parse_delegate_t* input = options.create_input();

    //setup the middle
    middle_t* middle = options.create_middle();

    //setup the backend (output)
    std::vector<output_t*> outputs = options.create_output(middle);

    //let osmdata orchestrate between the middle and the outs
    osmdata_t osmdata(middle, outputs.front());

    fprintf(stderr, "Using projection SRS %d (%s)\n", 
    		options.projection->project_getprojinfo()->srs,
    		options.projection->project_getprojinfo()->descr );

    //start it up
    time_t overall_start = time(NULL);
    outputs.front()->start();

    //read in the input files one by one
    while (optind < argc) {
        //read the actual input
        fprintf(stderr, "\nReading in file: %s\n", argv[optind]);
        time_t start = time(NULL);
        if (input->streamFile(options.input_reader, argv[optind], options.sanitize, &osmdata) != 0)
            util::exit_nicely();
        fprintf(stderr, "  parse time: %ds\n", (int)(time(NULL) - start));
        optind++;
    }

    //show stats
    input->printSummary();

    /* done with output_*_t */
    outputs.front()->stop();
    outputs.front()->cleanup();
    for(std::vector<output_t*>::iterator output = outputs.begin(); output != outputs.end(); ++output)
        delete *output;

    /* done with middle_*_t */
    delete middle;

    text_exit();
    fprintf(stderr, "\nOsm2pgsql took %ds overall\n", (int)(time(NULL) - overall_start));

    return 0;
}
