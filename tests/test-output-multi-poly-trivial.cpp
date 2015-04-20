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

void run_osm2pgsql(options_t &options) {
  //setup the front (input)
  parse_delegate_t parser(options.extra_attributes, options.bbox, options.projection);

  //setup the middle
  boost::shared_ptr<middle_t> middle = middle_t::create_middle(options.slim);

  //setup the backend (output)
  std::vector<boost::shared_ptr<output_t> > outputs = output_t::create_outputs(middle.get(), options);

  //let osmdata orchestrate between the middle and the outs
  osmdata_t osmdata(middle, outputs);

  osmdata.start();

  if (parser.streamFile("libxml2", "tests/test_output_multi_poly_trivial.osm", options.sanitize, &osmdata) != 0) {
    throw std::runtime_error("Unable to read input file `tests/test_output_multi_poly_trivial.osm'.");
  }

  osmdata.stop();
}

void check_output_poly_trivial(int enable_multi, std::string conninfo) {
  options_t options;
  options.conninfo = conninfo.c_str();
  options.num_procs = 1;
  options.slim = 1;
  options.enable_multi = enable_multi;

  options.projection.reset(new reprojection(PROJ_LATLONG));

  options.output_backend = "multi";
  options.style = "tests/test_output_multi_poly_trivial.style.json";

  run_osm2pgsql(options);

  // start a new connection to run tests on
  pg::conn_ptr test_conn = pg::conn::connect(conninfo);

  // expect that the table exists
  check_count(test_conn, 1, "select count(*) from pg_catalog.pg_class where relname = 'test_poly'");

  // expect 2 polygons if not in multi(geometry) mode, or 1 if multi(geometry)
  // mode is enabled.
  if (enable_multi) {
    check_count(test_conn, 1, "select count(*) from test_poly");
    check_count(test_conn, 1, "select count(*) from test_poly where foo='bar'");

    // this should be a 2-pointed multipolygon
    check_count(test_conn, 5, "select distinct st_numpoints(st_exteriorring(way)) from (select (st_dump(way)).geom as way from test_poly) x");

  } else {
    check_count(test_conn, 2, "select count(*) from test_poly");
    check_count(test_conn, 2, "select count(*) from test_poly where foo='bar'");

    // although there are 2 rows, they should both be 4-pointed polygons
    check_count(test_conn, 5, "select distinct st_numpoints(st_exteriorring(way)) from test_poly");
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
        check_output_poly_trivial(0, db->conninfo());
        check_output_poly_trivial(1, db->conninfo());
        return 0;

    } catch (const std::exception &e) {
        std::cerr << "ERROR: " << e.what() << std::endl;

    } catch (...) {
        std::cerr << "UNKNOWN ERROR" << std::endl;
    }

    return 1;
}
