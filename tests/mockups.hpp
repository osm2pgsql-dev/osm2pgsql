#ifndef TESTS_MOCKUPS_HPP
#define TESTS_MOCKUPS_HPP

#include "middle.hpp"
#include "output-null.hpp"

struct dummy_middle_t : public middle_t {
    virtual ~dummy_middle_t() = default;

    void start(const options_t *) override { }
    void stop(osmium::thread::Pool &) override {}
    void cleanup(void) { }
    void analyze(void) override  { }
    void end(void) override  { }
    void commit(void) override  { }

    void nodes_set(osmium::Node const &) override {}
    size_t nodes_get_list(osmium::WayNodeList *) const override { return 0; }

    void ways_set(osmium::Way const &) override { }
    bool ways_get(osmid_t, osmium::memory::Buffer &) const override { return true; }
    size_t rel_way_members_get(osmium::Relation const &, rolelist_t *,
                               osmium::memory::Buffer &) const override
    {
        return 0;
    }

    void relations_set(osmium::Relation const &) override { }
    bool relations_get(osmid_t, osmium::memory::Buffer &) const override { return 0; }

    void iterate_ways(pending_processor&) override  { }
    void iterate_relations(pending_processor&) override  { }

    virtual size_t pending_count() const override  { return 0; }

    idlist_t relations_using_way(osmid_t) const override  { return idlist_t(); }

    std::shared_ptr<const middle_query_t> get_instance() const override
    {
        return std::shared_ptr<const middle_query_t>();
    }
};

struct dummy_slim_middle_t : public slim_middle_t {
    virtual ~dummy_slim_middle_t() = default;

    void start(const options_t *) override  { }
    void stop(osmium::thread::Pool &) override {}
    void cleanup(void) { }
    void analyze(void) override  { }
    void end(void) override  { }
    void commit(void) override  { }

    void nodes_set(osmium::Node const &) override {}
    size_t nodes_get_list(osmium::WayNodeList *) const override { return 0; }

    void ways_set(osmium::Way const &) override { }
    bool ways_get(osmid_t, osmium::memory::Buffer &) const override { return true; }
    size_t rel_way_members_get(osmium::Relation const &, rolelist_t *,
                               osmium::memory::Buffer &) const override
    {
        return 0;
    }

    void relations_set(osmium::Relation const &) override { }
    bool relations_get(osmid_t, osmium::memory::Buffer &) const override { return 0; }

    void iterate_ways(pending_processor&) override  { }
    void iterate_relations(pending_processor&) override  { }

    size_t pending_count() const override  { return 0; }

    idlist_t relations_using_way(osmid_t) const override  { return idlist_t(); }

    std::shared_ptr<const middle_query_t> get_instance() const override  {return std::shared_ptr<const middle_query_t>();}

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

