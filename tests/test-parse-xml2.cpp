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

struct test_output_t : public output_t {
    uint64_t sum_ids, num_nodes, num_ways, num_relations, num_nds, num_members;

    test_output_t(middle_t* mid_,const options_t* options_)
        : output_t(mid_, options_), sum_ids(0), num_nodes(0), num_ways(0), num_relations(0),
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
    void stop() { }
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

  options_t options; memset(&options, 0, sizeof options);
  boost::shared_ptr<reprojection> projection(new reprojection(PROJ_SPHERE_MERC));
  options.projection = projection;

  struct test_output_t out_test(NULL, &options);
  struct osmdata_t osmdata(NULL, &out_test);

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
