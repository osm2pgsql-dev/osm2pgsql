#include <iostream>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <cassert>
#include <stdexcept>

#include "osmtypes.hpp"
#include "output-null.hpp"
#include "options.hpp"
#include "middle-ram.hpp"

#include "tests/middle-tests.hpp"

void run_tests(const options_t options, const std::string cache_type) {
  {
    middle_ram_t mid_ram;
    output_null_t out_test(&mid_ram, options);

    mid_ram.start(&options);

    if (test_node_set(&mid_ram) != 0) { throw std::runtime_error("test_node_set failed with " + cache_type + " cache."); }
    osmium::thread::Pool pool(1);
    mid_ram.commit();
    mid_ram.stop(pool);
  }
  {
    middle_ram_t mid_ram;
    output_null_t out_test(&mid_ram, options);

    mid_ram.start(&options);

    if (test_nodes_comprehensive_set(&mid_ram) != 0) { throw std::runtime_error("test_nodes_comprehensive_set failed with " + cache_type + " cache."); }
    osmium::thread::Pool pool(1);
    mid_ram.commit();
    mid_ram.stop(pool);
  }
  {
    middle_ram_t mid_ram;
    output_null_t out_test(&mid_ram, options);

    mid_ram.start(&options);

    if (test_way_set(&mid_ram) != 0) { throw std::runtime_error("test_way_set failed with " + cache_type + " cache."); }
    osmium::thread::Pool pool(1);
    mid_ram.commit();
    mid_ram.stop(pool);
  }
}

int main(int argc, char *argv[]) {
  try {
    options_t options;
    options.cache = 1; // Non-zero cache is needed to test

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
