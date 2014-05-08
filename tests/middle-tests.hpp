#ifndef TESTS_MIDDLE_TEST_HPP
#define TESTS_MIDDLE_TEST_HPP

extern "C" {
#include "middle.h"
}

// tests that a single node can be set and retrieved. returns 0 on success.
int test_node_set(middle_t *mid);

// tests that a single way and supporting nodes can be set and retrieved.
// returns 0 on success.
int test_way_set(middle_t *mid);

#endif /* TESTS_MIDDLE_TEST_HPP */
