#ifndef TESTS_MOCKUPS_HPP
#define TESTS_MOCKUPS_HPP

#include "middle.hpp"
#include "output-null.hpp"

struct dummy_middle_t : public middle_t {
    virtual ~dummy_middle_t() = default;

    void start() override {}
    void stop(osmium::thread::Pool &) override {}
    void flush(osmium::item_type) override {}
    void cleanup(void) { }
    void analyze(void) override  { }
    void commit(void) override  { }

    void nodes_set(osmium::Node const &) override {}

    void ways_set(osmium::Way const &) override { }

    void relations_set(osmium::Relation const &) override { }

    void iterate_ways(pending_processor&) override  { }
    void iterate_relations(pending_processor&) override  { }

    virtual size_t pending_count() const override  { return 0; }

    std::shared_ptr<middle_query_t>
    get_query_instance(std::shared_ptr<middle_t> const &) const override
    {
        return std::shared_ptr<middle_query_t>();
    }
};

struct dummy_slim_middle_t : public slim_middle_t {
    virtual ~dummy_slim_middle_t() = default;

    void start() override {}
    void stop(osmium::thread::Pool &) override {}
    void flush(osmium::item_type) override {}
    void cleanup(void) { }
    void analyze(void) override  { }
    void commit(void) override  { }

    void nodes_set(osmium::Node const &) override {}

    void ways_set(osmium::Way const &) override { }

    void relations_set(osmium::Relation const &) override { }

    void iterate_ways(pending_processor&) override  { }
    void iterate_relations(pending_processor&) override  { }

    size_t pending_count() const override  { return 0; }

    std::shared_ptr<middle_query_t>
    get_query_instance(std::shared_ptr<middle_t> const &) const override
    {
        return std::shared_ptr<middle_query_t>();
    }

    void nodes_delete(osmid_t) override  {};
    void node_changed(osmid_t) override  {};

    void ways_delete(osmid_t) override  {};
    void way_changed(osmid_t) override  {};

    void relations_delete(osmid_t) override  {};
    void relation_changed(osmid_t) override  {};
};

struct dummy_output_t : public output_null_t {

    explicit dummy_output_t(const options_t &options_)
        : output_null_t(nullptr, options_) {
    }

    ~dummy_output_t() = default;
};

#endif

