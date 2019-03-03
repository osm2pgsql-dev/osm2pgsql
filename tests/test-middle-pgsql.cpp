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

#include <sys/types.h>
#include <unistd.h>

#include "tests/middle-tests.hpp"
#include "tests/common-pg.hpp"

static void run_tests(options_t options)
{
  options.append = false;
  options.create = true;
  options.slim = true;
  {
      test_middle_helper t(options);

      if (t.test_node_set() != 0) {
          throw std::runtime_error("test_node_set failed.");
      }
  }

  {
      test_middle_helper t(options);

      if (t.test_nodes_comprehensive_set() != 0) {
          throw std::runtime_error("test_nodes_comprehensive_set failed.");
      }
  }

  {
      // First make sure we have an empty table.
      {
          test_middle_helper t(options);
      }

      // Then switch to append mode because this tests updates.
      options.append = true;
      options.create = false;

      test_middle_helper t(options);

      if (t.test_way_set() != 0) {
          throw std::runtime_error("test_way_set failed.");
      }
  }
}
int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
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
    options.cache = 1;
    options.num_procs = 1;
    options.prefix = "osm2pgsql_test";
    options.slim = true;

    options.alloc_chunkwise = ALLOC_SPARSE | ALLOC_DENSE; // what you get with optimized
    run_tests(options);
    options.alloc_chunkwise = ALLOC_SPARSE;
    run_tests(options);

    options.alloc_chunkwise = ALLOC_DENSE;
    run_tests(options);

    options.alloc_chunkwise = ALLOC_DENSE | ALLOC_DENSE_CHUNK; // what you get with chunk
    run_tests(options);
  } catch (const std::exception &e) {
    std::cerr << "ERROR: " << e.what() << std::endl;
    return 1;
  } catch (...) {
    std::cerr << "UNKNOWN ERROR" << std::endl;
    return 1;
  }
  return 0;
}
