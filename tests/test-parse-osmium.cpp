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
    void start() override {}
    void stop(thread_pool_t &) override {}
    void flush() override {}
    void cleanup() {}
    void analyze() override {}
    void commit() override {}

    void node_set(osmium::Node const &) override { ++node.added; }
    void way_set(osmium::Way const &) override { ++way.added; }
    void relation_set(osmium::Relation const &) override { ++relation.added; }

    std::shared_ptr<middle_query_t> get_query_instance() override
    {
        return std::shared_ptr<middle_query_t>{};
    }

    void node_delete(osmid_t) override { ++node.deleted; }
    void way_delete(osmid_t) override { ++way.deleted; }
    void relation_delete(osmid_t) override { ++relation.deleted; }

    type_stats_t node, way, relation;
};

struct counting_output_t : public output_null_t
{
    explicit counting_output_t(options_t const &options)
    : output_null_t(nullptr, options)
    {}

    std::shared_ptr<output_t>
    clone(std::shared_ptr<middle_query_t> const &,
          std::shared_ptr<db_copy_thread_t> const &) const override
    {
        return std::shared_ptr<output_t>(new counting_output_t{m_options});
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

struct counts_t {
    std::size_t nodes_changed = 0;
    std::size_t ways_changed = 0;
};

/**
 * This pseudo-dependency manager is just used for testing. It counts how
 * often the *_changed() member functions are called.
 */
class counting_dependency_manager_t : public dependency_manager_t
{
public:
    counting_dependency_manager_t(std::shared_ptr<counts_t> counts)
    : m_counts(std::move(counts))
    {}

    void node_changed(osmid_t) override { ++m_counts->nodes_changed; }
    void way_changed(osmid_t) override { ++m_counts->ways_changed; }

private:
    std::shared_ptr<counts_t> m_counts;
};

TEST_CASE("parse xml file")
{
    options_t const options = testing::opt_t().slim();

    auto const middle = std::make_shared<counting_slim_middle_t>();
    std::shared_ptr<output_t> output{new counting_output_t{options}};

    auto counts = std::make_shared<counts_t>();
    auto dependency_manager = std::unique_ptr<dependency_manager_t>(
        new counting_dependency_manager_t{counts});

    testing::parse_file(options, std::move(dependency_manager), middle,
                        {output}, "test_multipolygon.osm", false);

    auto const *out_test = static_cast<counting_output_t *>(output.get());
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

    auto const *mid_test = static_cast<counting_slim_middle_t *>(middle.get());
    REQUIRE(mid_test->node.added == 353);
    REQUIRE(mid_test->node.deleted == 0);
    REQUIRE(mid_test->way.added == 140);
    REQUIRE(mid_test->way.deleted == 0);
    REQUIRE(mid_test->relation.added == 40);
    REQUIRE(mid_test->relation.deleted == 0);

    REQUIRE(counts->nodes_changed == 0);
    REQUIRE(counts->ways_changed == 0);
}

TEST_CASE("parse diff file")
{
    options_t const options = testing::opt_t().slim().append();

    auto const middle = std::make_shared<counting_slim_middle_t>();
    std::shared_ptr<output_t> output{new counting_output_t{options}};

    auto counts = std::make_shared<counts_t>();
    auto dependency_manager = std::unique_ptr<dependency_manager_t>(
        new counting_dependency_manager_t{counts});

    testing::parse_file(options, std::move(dependency_manager), middle,
                        {output}, "008-ch.osc.gz", false);

    auto const *out_test = static_cast<counting_output_t *>(output.get());
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
    REQUIRE(mid_test->node.deleted == 17949);
    REQUIRE(mid_test->way.added == 161);
    REQUIRE(mid_test->way.deleted == 165);
    REQUIRE(mid_test->relation.added == 11);
    REQUIRE(mid_test->relation.deleted == 12);

    REQUIRE(counts->nodes_changed == 1176);
    REQUIRE(counts->ways_changed == 161);
}

TEST_CASE("parse xml file with extra args")
{
    options_t options = testing::opt_t().slim().srs(PROJ_SPHERE_MERC);
    options.extra_attributes = true;

    auto const middle = std::make_shared<counting_slim_middle_t>();
    std::shared_ptr<output_t> output{new counting_output_t{options}};

    auto counts = std::make_shared<counts_t>();
    auto dependency_manager = std::unique_ptr<dependency_manager_t>(
        new counting_dependency_manager_t{counts});

    testing::parse_file(options, std::move(dependency_manager), middle,
                        {output}, "test_multipolygon.osm", false);

    auto const *out_test = static_cast<counting_output_t *>(output.get());
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

    auto const *mid_test = static_cast<counting_slim_middle_t *>(middle.get());
    REQUIRE(mid_test->node.added == 353);
    REQUIRE(mid_test->node.deleted == 0);
    REQUIRE(mid_test->way.added == 140);
    REQUIRE(mid_test->way.deleted == 0);
    REQUIRE(mid_test->relation.added == 40);
    REQUIRE(mid_test->relation.deleted == 0);

    REQUIRE(counts->nodes_changed == 0);
    REQUIRE(counts->ways_changed == 0);
}
