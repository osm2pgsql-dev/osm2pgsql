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

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <stdexcept>
#include <libpq-fe.h>

#include "osmtypes.hpp"
#include "build_geometry.hpp"
#include "middle-pgsql.hpp"
#include "middle-ram.hpp"
#include "node-ram-cache.hpp"
#include "output-pgsql.hpp"
#include "output-gazetteer.hpp"
#include "output-null.hpp"
#include "sanitizer.hpp"
#include "reprojection.hpp"
#include "text-tree.hpp"
#include "input.hpp"
#include "parse.hpp"

#ifdef BUILD_READER_PBF
#  include "parse-pbf.hpp"
#endif

void exit_nicely()
{
    fprintf(stderr, "Error occurred, cleaning up\n");
    exit(1);
}

output_t* get_output(const char* output_backend, const options_t* options, middle_t* mid)
{
	if (strcmp("pgsql", output_backend) == 0) {
	  return new output_pgsql_t(mid, options);
	} else if (strcmp("gazetteer", output_backend) == 0) {
	  return new output_gazetteer_t(mid, options);
	} else if (strcmp("null", output_backend) == 0) {
	  return new output_null_t(mid, options);
	} else {
	  fprintf(stderr, "Output backend `%s' not recognised. Should be one of [pgsql, gazetteer, null].\n", output_backend);
	  exit(EXIT_FAILURE);
	}
}

int main(int argc, char *argv[])
{
    // Parse the args into the different outputs
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

    LIBXML_TEST_VERSION;

    //setup the middle
    middle_t* mid = options.slim ? ((middle_t *)new middle_pgsql_t()) : ((middle_t *)new middle_ram_t());

    //setup the backend (output)
    output_t* out = get_output(options.output_backend, &options, mid);

    osmdata_t osmdata(mid, out);

    //setup the front (input)
    parse_delegate_t parser(options.extra_attributes, options.bbox, options.projection);

    fprintf(stderr, "Using projection SRS %d (%s)\n", 
    		options.projection->project_getprojinfo()->srs,
    		options.projection->project_getprojinfo()->descr );

    //start it up
    time_t overall_start = time(NULL);
    out->start();

    //read in the input files one by one
    while (optind < argc) {
        //read the actual input
        fprintf(stderr, "\nReading in file: %s\n", argv[optind]);
        time_t start = time(NULL);
        if (parser.streamFile(options.input_reader, argv[optind], options.sanitize, &osmdata) != 0)
            exit_nicely();
        fprintf(stderr, "  parse time: %ds\n", (int)(time(NULL) - start));
        optind++;
    }

    xmlCleanupParser();
    xmlMemoryDump();
    
    //show stats
    parser.printSummary();

    /* done with output_*_t */
    out->stop();
    out->cleanup();
    delete out;

    /* done with middle_*_t */
    delete mid;

    /* free the column pointer buffer */
    free(options.hstore_columns);

    text_exit();
    fprintf(stderr, "\nOsm2pgsql took %ds overall\n", (int)(time(NULL) - overall_start));

    return 0;
}
