#include <iostream>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <cassert>
#include <sstream>
#include <stdexcept>
#include <memory>

#include "osmtypes.hpp"
#include "osmdata.hpp"
#include "middle.hpp"
#include "output-multi.hpp"
#include "options.hpp"
#include "taginfo_impl.hpp"
#include "parse.hpp"

#include <sys/types.h>
#include <unistd.h>

#include <boost/lexical_cast.hpp>

#include "tests/middle-tests.hpp"
#include "tests/common-pg.hpp"

int main(int argc, char *argv[]) {
    std::unique_ptr<pg::tempdb> db;

    try {
        db.reset(new pg::tempdb);
    } catch (const std::exception &e) {
        std::cerr << "Unable to setup database: " << e.what() << "\n";
        return 77; // <-- code to skip this test.
    }

    try {
        options_t options;
        options.database_options = db->database_options;
        options.num_procs = 1;
        options.slim = true;

        options.projection.reset(new reprojection(PROJ_LATLONG));

        options.output_backend = "multi";
        options.style = "tests/test_output_multi_line_trivial.style.json";

        //setup the front (input)
        parse_delegate_t parser(options.extra_attributes, options.bbox, options.projection, options.append);

        //setup the middle
        std::shared_ptr<middle_t> middle = middle_t::create_middle(options.slim);

        //setup the backend (output)
        std::vector<std::shared_ptr<output_t> > outputs = output_t::create_outputs(middle.get(), options);

        //let osmdata orchestrate between the middle and the outs
        osmdata_t osmdata(middle, outputs);

        osmdata.start();

        parser.stream_file("libxml2", "tests/test_output_multi_line_storage.osm", &osmdata);

        osmdata.stop();

        db->check_count(1, "select count(*) from pg_catalog.pg_class where relname = 'test_line'");
        db->check_count(3, "select count(*) from test_line");

        //check that we have the number of vertexes in each linestring
        db->check_count(3, "SELECT ST_NumPoints(way) FROM test_line WHERE osm_id = 1");
        db->check_count(2, "SELECT ST_NumPoints(way) FROM test_line WHERE osm_id = 2");
        db->check_count(2, "SELECT ST_NumPoints(way) FROM test_line WHERE osm_id = 3");

        db->check_count(3, "SELECT COUNT(*) FROM test_line WHERE foo = 'bar'");
        return 0;

    } catch (const std::exception &e) {
        std::cerr << "ERROR: " << e.what() << std::endl;

    } catch (...) {
        std::cerr << "UNKNOWN ERROR" << std::endl;
    }

    return 1;
}
