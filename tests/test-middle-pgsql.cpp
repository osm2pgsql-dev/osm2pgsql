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
#include "output-null.hpp"
#include "options.hpp"
#include "middle-pgsql.hpp"

#include <libpq-fe.h>
#include <sys/types.h>
#include <unistd.h>

#include <boost/scoped_ptr.hpp>

#include "tests/middle-tests.hpp"
#include "tests/common-pg.hpp"

int main(int argc, char *argv[]) {
  boost::scoped_ptr<pg::tempdb> db;

  try {
    db.reset(new pg::tempdb);
  } catch (const std::exception &e) {
    std::cerr << "Unable to setup database: " << e.what() << "\n";
    return 77; // <-- code to skip this test.
  }

  struct middle_pgsql_t mid_pgsql;
  options_t options;
  options.conninfo = db->conninfo().c_str();
  options.scale = 10000000;
  options.num_procs = 1;
  options.prefix = "osm2pgsql_test";
  options.tblsslim_index = "tablespacetest";
  options.tblsslim_data = "tablespacetest";
  options.slim = 1;

  struct output_null_t out_test(&mid_pgsql, options);

  try {
    // start an empty table to make the middle create the
    // tables it needs. we then run the test in "append" mode.
    mid_pgsql.start(&options);
    mid_pgsql.commit();
    mid_pgsql.stop();

    options.append = 1; /* <- needed because we're going to change the
                         *    data and check that the updates fire. */

    mid_pgsql.start(&options);

    int status = 0;

    status = test_node_set(&mid_pgsql);
    if (status != 0) { mid_pgsql.stop(); throw std::runtime_error("test_node_set failed."); }

    status = test_way_set(&mid_pgsql);
    if (status != 0) { mid_pgsql.stop(); throw std::runtime_error("test_way_set failed."); }

    mid_pgsql.commit();
    mid_pgsql.stop();

    return 0;

  } catch (const std::exception &e) {
    std::cerr << "ERROR: " << e.what() << std::endl;

  } catch (...) {
    std::cerr << "UNKNOWN ERROR" << std::endl;
  }

  return 1;
}
