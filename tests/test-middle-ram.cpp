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

void run_tests(const options_t options, const std::string cache_type)
{
    {
        test_middle_helper t(options);

        if (t.test_node_set() != 0) {
            throw std::runtime_error("test_node_set failed with " + cache_type +
                                     " cache.");
        }
    }

    {
        test_middle_helper t(options);

        if (t.test_nodes_comprehensive_set() != 0) {
            throw std::runtime_error(
                "test_nodes_comprehensive_set failed with " + cache_type +
                " cache.");
        }
    }

    {
        test_middle_helper t(options);

        if (t.test_way_set() != 0) {
            throw std::runtime_error("test_way_set failed with " + cache_type +
                                     " cache.");
        }
    }
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    try {
        options_t options;
        options.cache = 1; // Non-zero cache is needed to test
        options.slim = false;

        options.alloc_chunkwise =
            ALLOC_SPARSE | ALLOC_DENSE; // what you get with optimized
        run_tests(options, "optimized");

        options.alloc_chunkwise = ALLOC_SPARSE;
        run_tests(options, "sparse");

        options.alloc_chunkwise = ALLOC_DENSE;
        run_tests(options, "dense");

        options.alloc_chunkwise =
            ALLOC_DENSE | ALLOC_DENSE_CHUNK; // what you get with chunk
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
