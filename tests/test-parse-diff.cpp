#include <iostream>
#include <memory>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "middle.hpp"
#include "tests/mockups.hpp"
#include "options.hpp"
#include "osmdata.hpp"
#include "osmtypes.hpp"
#include "output.hpp"
#include "parse-osmium.hpp"

void exit_nicely()
{
    fprintf(stderr, "Error occurred, cleaning up\n");
    exit(1);
}

struct type_stats {
    unsigned added = 0;
    unsigned modified = 0;
    unsigned deleted = 0;
};

struct test_output_t : public dummy_output_t {
    type_stats node, way, rel;

    explicit test_output_t(const options_t &options_)
        : dummy_output_t(options_) {
    }

    virtual ~test_output_t() = default;

    std::shared_ptr<output_t> clone(const middle_query_t *cloned_middle) const{
        test_output_t *clone = new test_output_t(m_options);
        clone->m_mid = cloned_middle;
        return std::shared_ptr<output_t>(clone);
    }

    int node_add(osmid_t id, double, double, const taglist_t &) {
        assert(id > 0);
        ++node.added;
        return 0;
    }

    int way_add(osmid_t id, const idlist_t &, const taglist_t &) {
        assert(id > 0);
        ++way.added;
        return 0;
    }

    int relation_add(osmid_t id, const memberlist_t &, const taglist_t &) {
        assert(id > 0);
        ++rel.added;
        return 0;
    }

    int node_modify(osmid_t, double, double, const taglist_t &) {
        ++node.modified;
        return 0;
    }
    int way_modify(osmid_t, const idlist_t &, const taglist_t &) {
        ++way.modified;
        return 0;
    }
    int relation_modify(osmid_t, const memberlist_t &, const taglist_t &) {
        ++rel.modified;
        return 0;
    }

    int node_delete(osmid_t) {
        ++node.deleted;
        return 0;
    }
    int way_delete(osmid_t) {
        ++way.deleted;
        return 0;
    }
    int relation_delete(osmid_t) {
        ++rel.deleted;
        return 0;
    }

};


void assert_equal(uint64_t actual, uint64_t expected) {
  if (actual != expected) {
    std::cerr << "Expected " << expected << ", but got " << actual << ".\n";
    exit(1);
  }
}

int main() {

  std::string inputfile = "tests/008-ch.osc.gz";

  options_t options;

  std::shared_ptr<reprojection> projection(reprojection::create_projection(PROJ_SPHERE_MERC));
  options.projection = projection;

  auto out_test = std::make_shared<test_output_t>(options);
  osmdata_t osmdata(std::make_shared<dummy_slim_middle_t>(), out_test);

  boost::optional<std::string> bbox;
  parse_osmium_t parser(false, bbox, projection.get(), true, &osmdata);

  parser.stream_file(inputfile, "");

  assert_equal(out_test->node.added, 0);
  assert_equal(out_test->node.modified, 1176);
  assert_equal(out_test->node.deleted, 16773);
  assert_equal(out_test->way.added, 0);
  assert_equal(out_test->way.modified, 161);
  assert_equal(out_test->way.deleted, 4);
  assert_equal(out_test->rel.added, 0);
  assert_equal(out_test->rel.modified, 11);
  assert_equal(out_test->rel.deleted, 1);

  return 0;
}
