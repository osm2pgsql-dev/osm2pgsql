#include <iostream>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <cassert>

#include "osmtypes.hpp"
#include "parse-xml2.hpp"
#include "output.hpp"
#include "text-tree.hpp"

void exit_nicely()
{
    fprintf(stderr, "Error occurred, cleaning up\n");
    exit(1);
}

static uint64_t sum_ids, num_nodes, num_ways, num_relations, num_nds, num_members;

int test_node_add(osmid_t id, double lat, double lon, struct keyval *tags)
{
  assert(id > 0);
  sum_ids += id;
  num_nodes += 1;
  return 0;
}

int test_way_add(osmid_t id, osmid_t *nodes, int node_count, struct keyval *tags)
{
  assert(id > 0);
  sum_ids += id;
  num_ways += 1;
  assert(node_count >= 0);
  num_nds += uint64_t(node_count);
  return 0;
}

int test_relation_add(osmid_t id, struct member *members, int member_count, struct keyval *tags)
{
  assert(id > 0);
  sum_ids += id;
  num_relations += 1;
  assert(member_count >= 0);
  num_members += uint64_t(member_count);
  return 0;
}

void assert_equal(uint64_t actual, uint64_t expected) {
  if (actual != expected) {
    std::cerr << "Expected " << expected << ", but got " << actual << ".\n";
    exit(1);
  }
}

int main(int argc, char *argv[]) {
  char *srcdir = getenv("srcdir");
  
  if (srcdir == NULL) {
    std::cerr << "$srcdir not set!\n";
    return 1;
  } 

  std::string inputfile = std::string(srcdir) + std::string("/tests/test_multipolygon.osm");

  sum_ids = 0;
  num_nodes = 0;
  num_ways = 0;
  num_relations = 0;
  num_nds = 0;
  num_members = 0;

  // need this to avoid segfault!
  text_init();

  struct output_t out_test; memset(&out_test, 0, sizeof out_test);
  struct osmdata_t osmdata; memset(&osmdata, 0, sizeof osmdata);
  struct output_options options; memset(&options, 0, sizeof options);

  out_test.node_add = &test_node_add;
  out_test.way_add = &test_way_add;
  out_test.relation_add = &test_relation_add;

  osmdata.filetype = FILETYPE_NONE;
  osmdata.action   = ACTION_NONE;
  osmdata.bbox     = NULL;
  osmdata.out = &out_test;

  initList(&osmdata.tags);
  realloc_nodes(&osmdata);
  realloc_members(&osmdata);

  options.out = osmdata.out;

  int ret = streamFileXML2(inputfile.c_str(), 0, &osmdata);
  if (ret != 0) {
    return ret;
  }

  assert_equal(sum_ids,       73514L);
  assert_equal(num_nodes,       353L);
  assert_equal(num_ways,        140L);
  assert_equal(num_relations,    40L);
  assert_equal(num_nds,         495L);
  assert_equal(num_members,     146L);
  
  return 0;
}
