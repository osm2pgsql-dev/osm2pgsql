#ifndef TESTS_MIDDLE_TEST_HPP
#define TESTS_MIDDLE_TEST_HPP

#include "middle/middle.hpp"

// tests that a single node can be set and retrieved. returns 0 on success.
int test_node_set(middle_t *mid);

// tests various combinations of nodes being set and retrieved to trigger different cache strategies. returns 0 on success.
int test_nodes_comprehensive_set(middle_t *mid);

// tests that a single way and supporting nodes can be set and retrieved.
// returns 0 on success.
int test_way_set(middle_t *mid);

#endif /* TESTS_MIDDLE_TEST_HPP */
