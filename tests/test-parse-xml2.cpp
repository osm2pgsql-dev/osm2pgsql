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
#include "output-null.hpp"
#include "parse-osmium.hpp"

void exit_nicely()
{
    fprintf(stderr, "Error occurred, cleaning up\n");
    exit(1);
}

struct test_output_t : public output_null_t {
    uint64_t sum_ids, num_nodes, num_ways, num_relations, num_nds, num_members;

    explicit test_output_t(const options_t &options_)
        : output_null_t(nullptr, options_), sum_ids(0), num_nodes(0), num_ways(0), num_relations(0),
          num_nds(0), num_members(0) {
    }

    explicit test_output_t(const test_output_t &other)
        : output_null_t(other.m_mid, other.m_options), sum_ids(0), num_nodes(0), num_ways(0), num_relations(0),
          num_nds(0), num_members(0) {
    }

    virtual ~test_output_t() {
    }

    std::shared_ptr<output_t>
    clone(std::shared_ptr<middle_query_t> const &mid,
          std::shared_ptr<db_copy_thread_t> const &) const override
    {
        test_output_t *clone = new test_output_t(*this);
        clone->m_mid = mid;
        return std::shared_ptr<output_t>(clone);
    }

    int node_add(osmium::Node const &node) override
    {
        assert(node.id() > 0);
        sum_ids += (unsigned)node.id();
        num_nodes += 1;
        return 0;
    }

    int way_add(osmium::Way *way) override {
        assert(way->id() > 0);
        sum_ids += (unsigned) way->id();
        num_ways += 1;
        num_nds += uint64_t(way->nodes().size());
        return 0;
    }

    int relation_add(osmium::Relation const &rel) override {
        assert(rel.id() > 0);
        sum_ids += (unsigned) rel.id();
        num_relations += 1;
        num_members += uint64_t(rel.members().size());
        return 0;
    }
};


void assert_equal(uint64_t actual, uint64_t expected) {
  if (actual != expected) {
    std::cerr << "Expected " << expected << ", but got " << actual << ".\n";
    exit(1);
  }
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    std::string inputfile = "tests/test_multipolygon.osm";

    options_t options;
    std::shared_ptr<reprojection> projection(
        reprojection::create_projection(PROJ_SPHERE_MERC));
    options.projection = projection;

    auto out_test = std::make_shared<test_output_t>(options);
    osmdata_t osmdata(std::make_shared<dummy_middle_t>(), out_test);

    boost::optional<std::string> bbox;
    parse_osmium_t parser(bbox, false, &osmdata);

    parser.stream_file(inputfile, "");

    assert_equal(out_test->sum_ids, 4728L);
    assert_equal(out_test->num_nodes, 0L);
    assert_equal(out_test->num_ways, 48L);
    assert_equal(out_test->num_relations, 40L);
    assert_equal(out_test->num_nds, 186L);
    assert_equal(out_test->num_members, 146L);

    return 0;
}
