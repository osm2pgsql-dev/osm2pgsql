#include <iostream>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <cassert>

#include "osmtypes.hpp"
#include "parse-xml2.hpp"
#include "output.hpp"
#include "options.hpp"
#include "text-tree.hpp"
#include "keyvals.hpp"

void exit_nicely()
{
    fprintf(stderr, "Error occurred, cleaning up\n");
    exit(1);
}

struct test_middle_t : public middle_t {
    virtual ~test_middle_t() {}

    int start(const options_t *out_options_) { return 0; }
    void stop(void) { }
    void cleanup(void) { }
    void analyze(void) { }
    void end(void) { }
    void commit(void) { }

    int nodes_set(osmid_t id, double lat, double lon, struct keyval *tags) { return 0; }
    int nodes_get_list(struct osmNode *out, osmid_t *nds, int nd_count) const { return 0; }

    int ways_set(osmid_t id, osmid_t *nds, int nd_count, struct keyval *tags) { return 0; }
    int ways_get(osmid_t id, struct keyval *tag_ptr, struct osmNode **node_ptr, int *count_ptr) const { return 0; }
    int ways_get_list(osmid_t *ids, int way_count, osmid_t **way_ids, struct keyval *tag_ptr, struct osmNode **node_ptr, int *count_ptr) const { return 0; }

    int relations_set(osmid_t id, struct member *members, int member_count, struct keyval *tags) { return 0; }
    int relations_get(osmid_t id, struct member **members, int *member_count, struct keyval *tags) const { return 0; }

    void iterate_ways(way_cb_func &cb) { }
    void iterate_relations(rel_cb_func &cb) { }

    std::vector<osmid_t> relations_using_way(osmid_t way_id) const { return std::vector<osmid_t>(); }
};

struct test_output_t : public output_t {
    uint64_t sum_ids, num_nodes, num_ways, num_relations, num_nds, num_members;

    explicit test_output_t(const options_t &options_)
        : output_t(NULL, options_), sum_ids(0), num_nodes(0), num_ways(0), num_relations(0),
          num_nds(0), num_members(0) {
    }

    virtual ~test_output_t() {
    }

    int node_add(osmid_t id, double lat, double lon, struct keyval *tags) {
        assert(id > 0);
        sum_ids += id;
        num_nodes += 1;
        return 0;
    }

    int way_add(osmid_t id, osmid_t *nodes, int node_count, struct keyval *tags) {
        assert(id > 0);
        sum_ids += id;
        num_ways += 1;
        assert(node_count >= 0);
        num_nds += uint64_t(node_count);
        return 0;
    }
    
    int relation_add(osmid_t id, struct member *members, int member_count, struct keyval *tags) {
        assert(id > 0);
        sum_ids += id;
        num_relations += 1;
        assert(member_count >= 0);
        num_members += uint64_t(member_count);
        return 0;
    }

    int start() { return 0; }
    int connect(int startTransaction) { return 0; }
    middle_t::way_cb_func *way_callback() { return NULL; }
    middle_t::rel_cb_func *relation_callback() { return NULL; }
    void stop() { }
    void commit() { }
    void cleanup(void) { }
    void close(int stopTransaction) { }

    int node_modify(osmid_t id, double lat, double lon, struct keyval *tags) { return 0; }
    int way_modify(osmid_t id, osmid_t *nodes, int node_count, struct keyval *tags) { return 0; }
    int relation_modify(osmid_t id, struct member *members, int member_count, struct keyval *tags) { return 0; }

    int node_delete(osmid_t id) { return 0; }
    int way_delete(osmid_t id) { return 0; }
    int relation_delete(osmid_t id) { return 0; }
};

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

  // need this to avoid segfault!
  text_init();

  options_t options;
  boost::shared_ptr<reprojection> projection(new reprojection(PROJ_SPHERE_MERC));
  options.projection = projection;

  struct test_middle_t mid_test;
  struct test_output_t out_test(options);
  struct osmdata_t osmdata(&mid_test, &out_test);

  keyval tags;
  initList(&tags);
  parse_xml2_t parser(0, false, projection, 0, 0, 0, 0, tags);

  int ret = parser.streamFile(inputfile.c_str(), 0, &osmdata);
  if (ret != 0) {
    return ret;
  }

  assert_equal(out_test.sum_ids,       73514L);
  assert_equal(out_test.num_nodes,       353L);
  assert_equal(out_test.num_ways,        140L);
  assert_equal(out_test.num_relations,    40L);
  assert_equal(out_test.num_nds,         495L);
  assert_equal(out_test.num_members,     146L);
  
  return 0;
}
