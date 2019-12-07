#include <catch.hpp>

#include "middle.hpp"
#include "output-null.hpp"

#include "common-import.hpp"
#include "common-options.hpp"

struct type_stats_t
{
    unsigned added = 0;
    unsigned modified = 0;
    unsigned deleted = 0;
};

struct counting_slim_middle_t : public slim_middle_t
{
    ~counting_slim_middle_t() = default;

    void start() override {}
    void stop(osmium::thread::Pool &) override {}
    void flush(osmium::item_type) override {}
    void cleanup() {}
    void analyze() override {}
    void commit() override {}

    void nodes_set(osmium::Node const &) override { node.added++; }
    void ways_set(osmium::Way const &) override { way.added++; }
    void relations_set(osmium::Relation const &) override { relation.added++; }

    void iterate_ways(pending_processor &) override {}
    void iterate_relations(pending_processor &) override {}
    size_t pending_count() const override { return 0; }

    std::shared_ptr<middle_query_t>
    get_query_instance(std::shared_ptr<middle_t> const &) const override
    {
        return std::shared_ptr<middle_query_t>();
    }

    void nodes_delete(osmid_t) override { node.deleted++; }
    void node_changed(osmid_t) override { node.modified++; }

    void ways_delete(osmid_t) override { way.deleted++; }
    void way_changed(osmid_t) override { way.modified++; }

    void relations_delete(osmid_t) override { relation.deleted++; }
    void relation_changed(osmid_t) override { relation.modified++; }

    type_stats_t node, way, relation;
};

struct counting_output_t : public output_null_t
{
    explicit counting_output_t(options_t const &options)
    : output_null_t(nullptr, options)
    {}

    virtual ~counting_output_t() = default;

    std::shared_ptr<output_t>
    clone(std::shared_ptr<middle_query_t> const &,
          std::shared_ptr<db_copy_thread_t> const &) const override
    {
        return std::shared_ptr<output_t>(new counting_output_t(m_options));
    }

    void node_add(osmium::Node const &n) override
    {
        ++node.added;
        sum_ids += n.id();
    }

    void way_add(osmium::Way *w) override
    {
        ++way.added;
        sum_ids += w->id();
        sum_nds += w->nodes().size();
    }

    void relation_add(osmium::Relation const &r) override
    {
        ++relation.added;
        sum_ids += r.id();
        sum_members += r.members().size();
    }

    void node_modify(osmium::Node const &) override { ++node.modified; }

    void way_modify(osmium::Way *) override { ++way.modified; }

    void relation_modify(osmium::Relation const &) override
    {
        ++relation.modified;
    }

    void node_delete(osmid_t) override { ++node.deleted; }

    void way_delete(osmid_t) override { ++way.deleted; }

    void relation_delete(osmid_t) override { ++relation.deleted; }

    type_stats_t node, way, relation;
    long long sum_ids = 0;
    unsigned sum_nds = 0;
    unsigned sum_members = 0;
};

TEST_CASE("parse xml file")
{
    options_t options = testing::opt_t().slim();

    auto middle = std::make_shared<counting_slim_middle_t>();
    std::shared_ptr<output_t> output(new counting_output_t(options));

    testing::parse_file(options, middle, {output}, "test_multipolygon.osm");

    auto *out_test = static_cast<counting_output_t *>(output.get());
    REQUIRE(out_test->sum_ids == 4728);
    REQUIRE(out_test->sum_nds == 186);
    REQUIRE(out_test->sum_members == 146);
    REQUIRE(out_test->node.added == 0);
    REQUIRE(out_test->node.modified == 0);
    REQUIRE(out_test->node.deleted == 0);
    REQUIRE(out_test->way.added == 48);
    REQUIRE(out_test->way.modified == 0);
    REQUIRE(out_test->way.deleted == 0);
    REQUIRE(out_test->relation.added == 40);
    REQUIRE(out_test->relation.modified == 0);
    REQUIRE(out_test->relation.deleted == 0);

    auto *mid_test = static_cast<counting_slim_middle_t *>(middle.get());
    REQUIRE(mid_test->node.added == 353);
    REQUIRE(mid_test->node.modified == 0);
    REQUIRE(mid_test->node.deleted == 0);
    REQUIRE(mid_test->way.added == 140);
    REQUIRE(mid_test->way.modified == 0);
    REQUIRE(mid_test->way.deleted == 0);
    REQUIRE(mid_test->relation.added == 40);
    REQUIRE(mid_test->relation.modified == 0);
    REQUIRE(mid_test->relation.deleted == 0);
}

TEST_CASE("parse diff file")
{
    options_t options = testing::opt_t().slim().append();

    auto middle = std::make_shared<counting_slim_middle_t>();
    std::shared_ptr<output_t> output(new counting_output_t(options));

    testing::parse_file(options, middle, {output}, "008-ch.osc.gz");

    auto *out_test = static_cast<counting_output_t *>(output.get());
    REQUIRE(out_test->node.added == 0);
    REQUIRE(out_test->node.modified == 1176);
    REQUIRE(out_test->node.deleted == 16773);
    REQUIRE(out_test->way.added == 0);
    REQUIRE(out_test->way.modified == 161);
    REQUIRE(out_test->way.deleted == 4);
    REQUIRE(out_test->relation.added == 0);
    REQUIRE(out_test->relation.modified == 11);
    REQUIRE(out_test->relation.deleted == 1);

    auto *mid_test = static_cast<counting_slim_middle_t *>(middle.get());
    REQUIRE(mid_test->node.added == 1176);
    REQUIRE(mid_test->node.modified == 1176);
    REQUIRE(mid_test->node.deleted == 17949);
    REQUIRE(mid_test->way.added == 161);
    REQUIRE(mid_test->way.modified == 161);
    REQUIRE(mid_test->way.deleted == 165);
    REQUIRE(mid_test->relation.added == 11);
    REQUIRE(mid_test->relation.modified == 11);
    REQUIRE(mid_test->relation.deleted == 12);
}

TEST_CASE("parse xml file with extra args")
{
    options_t options = testing::opt_t().slim().srs(PROJ_SPHERE_MERC);
    options.extra_attributes = true;

    auto middle = std::make_shared<counting_slim_middle_t>();
    std::shared_ptr<output_t> output(new counting_output_t(options));

    testing::parse_file(options, middle, {output}, "test_multipolygon.osm");

    auto *out_test = static_cast<counting_output_t *>(output.get());
    REQUIRE(out_test->sum_ids == 73514);
    REQUIRE(out_test->sum_nds == 495);
    REQUIRE(out_test->sum_members == 146);
    REQUIRE(out_test->node.added == 353);
    REQUIRE(out_test->node.modified == 0);
    REQUIRE(out_test->node.deleted == 0);
    REQUIRE(out_test->way.added == 140);
    REQUIRE(out_test->way.modified == 0);
    REQUIRE(out_test->way.deleted == 0);
    REQUIRE(out_test->relation.added == 40);
    REQUIRE(out_test->relation.modified == 0);
    REQUIRE(out_test->relation.deleted == 0);

    auto *mid_test = static_cast<counting_slim_middle_t *>(middle.get());
    REQUIRE(mid_test->node.added == 353);
    REQUIRE(mid_test->node.modified == 0);
    REQUIRE(mid_test->node.deleted == 0);
    REQUIRE(mid_test->way.added == 140);
    REQUIRE(mid_test->way.modified == 0);
    REQUIRE(mid_test->way.deleted == 0);
    REQUIRE(mid_test->relation.added == 40);
    REQUIRE(mid_test->relation.modified == 0);
    REQUIRE(mid_test->relation.deleted == 0);
}
