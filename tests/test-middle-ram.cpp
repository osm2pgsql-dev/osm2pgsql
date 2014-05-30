#include <iostream>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <cassert>
#include <stdexcept>

#include "osmtypes.hpp"
#include "middle.hpp"
#include "output-null.hpp"
#include "options.hpp"
#include "middle-ram.hpp"
#include "node-ram-cache.hpp"

#include "tests/middle-tests.hpp"

int main(int argc, char *argv[]) {
  try {
    options_t options;
    options.scale = 10000000;
    options.alloc_chunkwise = ALLOC_SPARSE | ALLOC_DENSE;
    options.cache = 1;

    struct middle_ram_t mid_ram;
    struct output_null_t out_test(&mid_ram, &options);
    
    mid_ram.start(&options);
    
    int status = 0;
    
    status = test_node_set(&mid_ram);
    if (status != 0) { throw std::runtime_error("test_node_set failed."); }
    
    status = test_way_set(&mid_ram);
    if (status != 0) { throw std::runtime_error("test_node_set failed."); }
    
    mid_ram.commit();
    mid_ram.stop();
  
    return 0;

  } catch (const std::exception &e) {
    std::cerr << "ERROR: " << e.what() << std::endl;

  } catch (...) {
    std::cerr << "UNKNOWN ERROR" << std::endl;
  }

  return 1;
}
