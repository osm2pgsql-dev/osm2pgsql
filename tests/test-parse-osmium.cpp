/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

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

struct counting_middle_t : public middle_t
{
    explicit counting_middle_t(bool append)
    : middle_t(nullptr), m_append(append)
    {}

    void start() override {}
    void stop() override {}
    void cleanup() {}

    void node(osmium::Node const &node) override {
        if (m_append) {
            ++node_count.deleted;
            if (!node.deleted()) {
                ++node_count.added;
            }
            return;
        }
        ++node_count.added;
    }

    void way(osmium::Way const &way) override {
        if (m_append) {
            ++way_count.deleted;
            if (!way.deleted()) {
                ++way_count.added;
            }
            return;
        }
        ++way_count.added;
    }

    void relation(osmium::Relation const &relation) override {
        if (m_append) {
            ++relation_count.deleted;
            if (!relation.deleted()) {
                ++relation_count.added;
            }
            return;
        }
        ++relation_count.added;
    }

    std::shared_ptr<middle_query_t> get_query_instance() override
    {
        return std::shared_ptr<middle_query_t>{};
    }

    type_stats_t node_count, way_count, relation_count;
    bool m_append;
};

struct counting_output_t : public output_null_t
{
    explicit counting_output_t(options_t const &options)
    : output_null_t(nullptr, nullptr, options)
    {}

    std::shared_ptr<output_t>
    clone(std::shared_ptr<middle_query_t> const &,
          std::shared_ptr<db_copy_thread_t> const &) const override
    {
        return std::make_shared<counting_output_t>(*get_options());
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
    explicit counting_dependency_manager_t(std::shared_ptr<counts_t> counts)
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

    auto const middle = std::make_shared<counting_middle_t>(false);
    auto const output = std::make_shared<counting_output_t>(options);

    auto counts = std::make_shared<counts_t>();
    auto dependency_manager =
        std::make_unique<counting_dependency_manager_t>(counts);

    testing::parse_file(options, std::move(dependency_manager), middle,
                        output, "test_multipolygon.osm", false);

    REQUIRE(output->sum_ids == 4728);
    REQUIRE(output->sum_nds == 186);
    REQUIRE(output->sum_members == 146);
    REQUIRE(output->node.added == 0);
    REQUIRE(output->node.modified == 0);
    REQUIRE(output->node.deleted == 0);
    REQUIRE(output->way.added == 48);
    REQUIRE(output->way.modified == 0);
    REQUIRE(output->way.deleted == 0);
    REQUIRE(output->relation.added == 40);
    REQUIRE(output->relation.modified == 0);
    REQUIRE(output->relation.deleted == 0);

    auto const *mid_test = middle.get();
    REQUIRE(mid_test->node_count.added == 353);
    REQUIRE(mid_test->node_count.deleted == 0);
    REQUIRE(mid_test->way_count.added == 140);
    REQUIRE(mid_test->way_count.deleted == 0);
    REQUIRE(mid_test->relation_count.added == 40);
    REQUIRE(mid_test->relation_count.deleted == 0);

    REQUIRE(counts->nodes_changed == 0);
    REQUIRE(counts->ways_changed == 0);
}

TEST_CASE("parse diff file")
{
    options_t const options = testing::opt_t().slim().append();

    auto const middle = std::make_shared<counting_middle_t>(true);
    auto const output = std::make_shared<counting_output_t>(options);

    auto counts = std::make_shared<counts_t>();
    auto dependency_manager =
        std::make_unique<counting_dependency_manager_t>(counts);

    testing::parse_file(options, std::move(dependency_manager), middle,
                        output, "008-ch.osc.gz", false);

    REQUIRE(output->node.added == 0);
    REQUIRE(output->node.modified == 1176);
    REQUIRE(output->node.deleted == 16773);
    REQUIRE(output->way.added == 0);
    REQUIRE(output->way.modified == 161);
    REQUIRE(output->way.deleted == 4);
    REQUIRE(output->relation.added == 0);
    REQUIRE(output->relation.modified == 11);
    REQUIRE(output->relation.deleted == 1);

    auto *mid_test = middle.get();
    REQUIRE(mid_test->node_count.added == 1176);
    REQUIRE(mid_test->node_count.deleted == 17949);
    REQUIRE(mid_test->way_count.added == 161);
    REQUIRE(mid_test->way_count.deleted == 165);
    REQUIRE(mid_test->relation_count.added == 11);
    REQUIRE(mid_test->relation_count.deleted == 12);

    REQUIRE(counts->nodes_changed == 1176);
    REQUIRE(counts->ways_changed == 161);
}

TEST_CASE("parse xml file with extra args")
{
    options_t options = testing::opt_t().slim().srs(PROJ_SPHERE_MERC);
    options.extra_attributes = true;

    auto const middle = std::make_shared<counting_middle_t>(false);
    auto const output = std::make_shared<counting_output_t>(options);

    auto counts = std::make_shared<counts_t>();
    auto dependency_manager =
        std::make_unique<counting_dependency_manager_t>(counts);

    testing::parse_file(options, std::move(dependency_manager), middle,
                        output, "test_multipolygon.osm", false);

    REQUIRE(output->sum_ids == 73514);
    REQUIRE(output->sum_nds == 495);
    REQUIRE(output->sum_members == 146);
    REQUIRE(output->node.added == 353);
    REQUIRE(output->node.modified == 0);
    REQUIRE(output->node.deleted == 0);
    REQUIRE(output->way.added == 140);
    REQUIRE(output->way.modified == 0);
    REQUIRE(output->way.deleted == 0);
    REQUIRE(output->relation.added == 40);
    REQUIRE(output->relation.modified == 0);
    REQUIRE(output->relation.deleted == 0);

    auto const *mid_test = middle.get();
    REQUIRE(mid_test->node_count.added == 353);
    REQUIRE(mid_test->node_count.deleted == 0);
    REQUIRE(mid_test->way_count.added == 140);
    REQUIRE(mid_test->way_count.deleted == 0);
    REQUIRE(mid_test->relation_count.added == 40);
    REQUIRE(mid_test->relation_count.deleted == 0);

    REQUIRE(counts->nodes_changed == 0);
    REQUIRE(counts->ways_changed == 0);
}

TEST_CASE("invalid location")
{
    options_t options = testing::opt_t();

    auto const middle = std::make_shared<counting_middle_t>(false);
    auto const output = std::make_shared<counting_output_t>(options);

    auto counts = std::make_shared<counts_t>();
    auto dependency_manager =
        std::make_unique<counting_dependency_manager_t>(counts);

    testing::parse_file(options, std::move(dependency_manager), middle,
                        output, "test_invalid_location.osm", false);

    REQUIRE(output->node.added == 0);
    REQUIRE(output->way.added == 0);
    REQUIRE(output->relation.added == 0);
}

