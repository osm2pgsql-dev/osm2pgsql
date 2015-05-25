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
#include "osmdata.hpp"
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
        options.style = "tests/test_output_multi_tags.json";

        //setup the front (input)
        parse_delegate_t parser(options.extra_attributes, options.bbox, options.projection);

        //setup the middle
        boost::shared_ptr<middle_t> middle = middle_t::create_middle(options.slim);

        //setup the backend (output)
        std::vector<boost::shared_ptr<output_t> > outputs = output_t::create_outputs(middle.get(), options);

        //let osmdata orchestrate between the middle and the outs
        osmdata_t osmdata(middle, outputs);

        osmdata.start();

        if (parser.streamFile("libxml2", "tests/test_output_multi_tags.osm", options.sanitize, &osmdata) != 0) {
            throw std::runtime_error("Unable to read input file `tests/test_output_multi_line_storage.osm'.");
        }

        osmdata.stop();

        // start a new connection to run tests on
        pg::conn_ptr test_conn = pg::conn::connect(db->conninfo());

        // Check we got the right tables
        check_count(test_conn, 1, "select count(*) from pg_catalog.pg_class where relname = 'test_points_1'");
        check_count(test_conn, 1, "select count(*) from pg_catalog.pg_class where relname = 'test_points_2'");
        check_count(test_conn, 1, "select count(*) from pg_catalog.pg_class where relname = 'test_line_1'");
        check_count(test_conn, 1, "select count(*) from pg_catalog.pg_class where relname = 'test_polygon_1'");
        check_count(test_conn, 1, "select count(*) from pg_catalog.pg_class where relname = 'test_polygon_2'");

        // Check we didn't get any extra in the tables
        check_count(test_conn, 2, "select count(*) from test_points_1");
        check_count(test_conn, 2, "select count(*) from test_points_2");
        check_count(test_conn, 1, "select count(*) from test_line_1");
        check_count(test_conn, 1, "select count(*) from test_line_2");
        check_count(test_conn, 1, "select count(*) from test_polygon_1");
        check_count(test_conn, 1, "select count(*) from test_polygon_2");

        // Check that the first table for each type got the right transform
        check_count(test_conn, 1, "SELECT COUNT(*) FROM test_points_1 WHERE foo IS NULL and bar = 'n1' AND baz IS NULL");
        check_count(test_conn, 1, "SELECT COUNT(*) FROM test_points_1 WHERE foo IS NULL and bar = 'n2' AND baz IS NULL");
        check_count(test_conn, 1, "SELECT COUNT(*) FROM test_line_1 WHERE foo IS NULL and bar = 'w1' AND baz IS NULL");
        check_count(test_conn, 1, "SELECT COUNT(*) FROM test_polygon_1 WHERE foo IS NULL and bar = 'w2' AND baz IS NULL");

        // Check that the second table also got the right transform
        check_count(test_conn, 1, "SELECT COUNT(*) FROM test_points_2 WHERE foo IS NULL and bar IS NULL AND baz = 'n1'");
        check_count(test_conn, 1, "SELECT COUNT(*) FROM test_points_2 WHERE foo IS NULL and bar IS NULL AND baz = 'n2'");
        check_count(test_conn, 1, "SELECT COUNT(*) FROM test_line_2 WHERE foo IS NULL and bar IS NULL AND baz = 'w1'");
        check_count(test_conn, 1, "SELECT COUNT(*) FROM test_polygon_2 WHERE foo IS NULL and bar IS NULL AND baz = 'w2'");

        return 0;

    } catch (const std::exception &e) {
        std::cerr << "ERROR: " << e.what() << std::endl;

    } catch (...) {
        std::cerr << "UNKNOWN ERROR" << std::endl;
    }

    return 1;
}
