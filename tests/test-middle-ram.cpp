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

int main(int argc, char *argv[]) {
  try {
    options_t options;
    options.scale = 10000000;
    options.cache = 1; // Non-zero cache is needed to test

    {
      options.alloc_chunkwise = ALLOC_SPARSE | ALLOC_DENSE; // optimized
      middle_ram_t mid_ram;
      output_null_t out_test(&mid_ram, options);

      mid_ram.start(&options);

      if (test_node_set(&mid_ram) != 0) { throw std::runtime_error("test_node_set failed with optimized cache."); }

      if (test_way_set(&mid_ram) != 0) { throw std::runtime_error("test_way_set failed with optimized cache."); }

      mid_ram.commit();
      mid_ram.stop();
    }
  } catch (const std::exception &e) {
    std::cerr << "ERROR: " << e.what() << std::endl;
    return 1;
  } catch (...) {
    std::cerr << "UNKNOWN ERROR" << std::endl;
    return 1;
  }

  return 0;
}
