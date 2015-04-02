#include <iostream>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <cassert>
#include <sstream>
#include <stdexcept>
#include <memory>

#include "osmtypes.hpp"
#include "middle.hpp"
#include "output-multi.hpp"
#include "options.hpp"
#include "middle-pgsql.hpp"
#include "taginfo_impl.hpp"
#include "parse.hpp"

#include <libpq-fe.h>
#include <sys/types.h>
#include <unistd.h>

#include <boost/scoped_ptr.hpp>
#include <boost/lexical_cast.hpp>

#include "tests/middle-tests.hpp"
#include "tests/common-pg.hpp"

void check_count(pg::conn_ptr &conn, int expected, const std::string &query) {
    pg::result_ptr res = conn->exec(query);

    int ntuples = PQntuples(res->get());
    if (ntuples != 1) {
        throw std::runtime_error((boost::format("Expected only one tuple from a query "
                                                "to check COUNT(*), but got %1%. Query "
                                                "was: %2%.")
                                  % ntuples % query).str());
    }

    std::string numstr = PQgetvalue(res->get(), 0, 0);
    int count = boost::lexical_cast<int>(numstr);

    if (count != expected) {
        throw std::runtime_error((boost::format("Expected %1%, but got %2%, when running "
                                                "query: %3%.")
                                  % expected % count % query).str());
    }
}

int main(int argc, char *argv[]) {
    boost::scoped_ptr<pg::tempdb> db;

    try {
        db.reset(new pg::tempdb);
    } catch (const std::exception &e) {
        std::cerr << "Unable to setup database: " << e.what() << "\n";
        return 77; // <-- code to skip this test.
    }

    try {
        options_t options;
        options.conninfo = db->conninfo().c_str();
        options.num_procs = 1;
        options.slim = 1;

        options.projection.reset(new reprojection(PROJ_LATLONG));

        options.output_backend = "multi";
        options.style = "tests/test_output_multi_line_trivial.style.json";

        //setup the front (input)
        parse_delegate_t parser(options.extra_attributes, options.bbox, options.projection);

        //setup the middle
        boost::shared_ptr<middle_t> middle = middle_t::create_middle(options.slim);

        //setup the backend (output)
        std::vector<boost::shared_ptr<output_t> > outputs = output_t::create_outputs(middle.get(), options);

        //let osmdata orchestrate between the middle and the outs
        osmdata_t osmdata(middle, outputs);

        osmdata.start();

        if (parser.streamFile("libxml2", "tests/test_output_multi_line_storage.osm", options.sanitize, &osmdata) != 0) {
            throw std::runtime_error("Unable to read input file `tests/test_output_multi_line_storage.osm'.");
        }

        osmdata.stop();

        // start a new connection to run tests on
        pg::conn_ptr test_conn = pg::conn::connect(db->conninfo());

        check_count(test_conn, 1, "select count(*) from pg_catalog.pg_class where relname = 'test_line'");
        check_count(test_conn, 3, "select count(*) from test_line");

        //check that we have the number of vertexes in each linestring
        check_count(test_conn, 3, "SELECT ST_NumPoints(way) FROM test_line WHERE osm_id = 1");
        check_count(test_conn, 2, "SELECT ST_NumPoints(way) FROM test_line WHERE osm_id = 2");
        check_count(test_conn, 2, "SELECT ST_NumPoints(way) FROM test_line WHERE osm_id = 3");

        return 0;

    } catch (const std::exception &e) {
        std::cerr << "ERROR: " << e.what() << std::endl;

    } catch (...) {
        std::cerr << "UNKNOWN ERROR" << std::endl;
    }

    return 1;
}
