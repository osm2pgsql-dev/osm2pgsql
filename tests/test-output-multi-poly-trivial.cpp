#include <iostream>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <cassert>
#include <sstream>
#include <stdexcept>
#include <memory>

#include "middle-pgsql.hpp"
#include "options.hpp"
#include "osmdata.hpp"
#include "osmtypes.hpp"
#include "output-multi.hpp"
#include "taginfo_impl.hpp"

#include <sys/types.h>
#include <unistd.h>

#include <boost/lexical_cast.hpp>

#include "tests/common-pg.hpp"
#include "tests/common.hpp"

void check_output_poly_trivial(bool enable_multi, std::shared_ptr<pg::tempdb> db) {
  options_t options;
  options.database_options = db->database_options;
  options.num_procs = 1;
  options.slim = 1;
  options.enable_multi = enable_multi;

  options.projection.reset(reprojection::create_projection(PROJ_LATLONG));

  options.output_backend = "multi";
  options.style = "tests/test_output_multi_poly_trivial.style.json";

  testing::run_osm2pgsql(options, "tests/test_output_multi_poly_trivial.osm",
                         "xml");

  // expect that the table exists
  db->check_count(1, "select count(*) from pg_catalog.pg_class where relname = 'test_poly'");

  // expect 2 polygons if not in multi(geometry) mode, or 1 if multi(geometry)
  // mode is enabled.
  if (enable_multi) {
    db->check_count(1, "select count(*) from test_poly");
    db->check_count(1, "select count(*) from test_poly where foo='bar'");
    db->check_count(1, "select count(*) from test_poly where bar='baz'");

    // there should be two 5-pointed polygons in the multipolygon (note that
    // it's 5 points including the duplicated first/last point)
    db->check_count(2, "select count(*) from (select (st_dump(way)).geom as way from test_poly) x");
    db->check_count(5, "select distinct st_numpoints(st_exteriorring(way)) from (select (st_dump(way)).geom as way from test_poly) x");

  } else {
    db->check_count(2, "select count(*) from test_poly");
    db->check_count(2, "select count(*) from test_poly where foo='bar'");
    db->check_count(2, "select count(*) from test_poly where bar='baz'");

    // although there are 2 rows, they should both be 5-pointed polygons (note
    // that it's 5 points including the duplicated first/last point)
    db->check_count(5, "select distinct st_numpoints(st_exteriorring(way)) from test_poly");
  }
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    std::shared_ptr<pg::tempdb> db;

    try {
        db.reset(new pg::tempdb);
    } catch (const std::exception &e) {
        std::cerr << "Unable to setup database: " << e.what() << "\n";
        return 77; // <-- code to skip this test.
    }

    try {
        check_output_poly_trivial(0, db);
        check_output_poly_trivial(1, db);
        return 0;

    } catch (const std::exception &e) {
        std::cerr << "ERROR: " << e.what() << std::endl;

    } catch (...) {
        std::cerr << "UNKNOWN ERROR" << std::endl;
    }

    return 1;
}
