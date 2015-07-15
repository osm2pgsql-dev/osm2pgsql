#include <iostream>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <cassert>
#include <sstream>
#include <stdexcept>
#include <memory>

#include "osmtypes.hpp"
#include "output-null.hpp"
#include "options.hpp"
#include "middle-pgsql.hpp"

#include <libpq-fe.h>
#include <sys/types.h>
#include <unistd.h>

#include <boost/scoped_ptr.hpp>

#include "tests/middle-tests.hpp"
#include "tests/common-pg.hpp"

void run_tests(options_t options, const std::string cache_type) {
  options.append = 0;
  options.create = 1;
  {
    middle_pgsql_t mid_pgsql;
    output_null_t out_test(&mid_pgsql, options);

    mid_pgsql.start(&options);

    if (test_node_set(&mid_pgsql) != 0) { throw std::runtime_error("test_node_set failed."); }

    mid_pgsql.commit();
    mid_pgsql.stop();
  }
  {
    middle_pgsql_t mid_pgsql;
    output_null_t out_test(&mid_pgsql, options);

    mid_pgsql.start(&options);

    if (test_nodes_comprehensive_set(&mid_pgsql) != 0) { throw std::runtime_error("test_nodes_comprehensive_set failed."); }

    mid_pgsql.commit();
    mid_pgsql.stop();
  }
  {
    middle_pgsql_t mid_pgsql;
    output_null_t out_test(&mid_pgsql, options);

    mid_pgsql.start(&options);
    mid_pgsql.commit();
    mid_pgsql.stop();
    // Switch to append mode because this tests updates
    options.append = 1;
    options.create = 0;
    mid_pgsql.start(&options);
    if (test_way_set(&mid_pgsql) != 0) { throw std::runtime_error("test_way_set failed."); }

    mid_pgsql.commit();
    mid_pgsql.stop();
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
    options.scale = 10000000;
    options.cache = 1;
    options.num_procs = 1;
    options.prefix = "osm2pgsql_test";
    options.slim = 1;

    options.alloc_chunkwise = ALLOC_SPARSE | ALLOC_DENSE; // what you get with optimized
    run_tests(options, "optimized");
    options.alloc_chunkwise = ALLOC_SPARSE;
    run_tests(options, "sparse");

    options.alloc_chunkwise = ALLOC_DENSE;
    run_tests(options, "dense");

    options.alloc_chunkwise = ALLOC_DENSE | ALLOC_DENSE_CHUNK; // what you get with chunk
    run_tests(options, "chunk");
  } catch (const std::exception &e) {
    std::cerr << "ERROR: " << e.what() << std::endl;
    return 1;
  } catch (...) {
    std::cerr << "UNKNOWN ERROR" << std::endl;
    return 1;
  }
  return 0;
}
