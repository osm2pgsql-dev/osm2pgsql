#ifndef TESTS_MOCKUPS_HPP
#define TESTS_MOCKUPS_HPP

#include "middle.hpp"
#include "output.hpp"

struct dummy_middle_t : public middle_t {
    virtual ~dummy_middle_t() = default;

    void start(const options_t *) { }
    void stop(void) { }
    void cleanup(void) { }
    void analyze(void) { }
    void end(void) { }
    void commit(void) { }

    void nodes_set(osmium::Node const &, double, double, bool) { }
    size_t nodes_get_list(nodelist_t &, const idlist_t) const { return 0; }

    void ways_set(osmium::Way const &, bool) { }
    bool ways_get(osmid_t, taglist_t &, nodelist_t &) const { return true; }
    size_t ways_get_list(const idlist_t &, idlist_t &,
                              std::vector<taglist_t> &,
                              std::vector<nodelist_t> &) const { return 0; }

    void relations_set(osmium::Relation const &, bool) { }
    bool relations_get(osmid_t, memberlist_t &, taglist_t &) const { return 0; }

    void iterate_ways(pending_processor&) { }
    void iterate_relations(pending_processor&) { }

    virtual size_t pending_count() const { return 0; }

    idlist_t relations_using_way(osmid_t) const { return idlist_t(); }

    virtual std::shared_ptr<const middle_query_t> get_instance() const {return std::shared_ptr<const middle_query_t>();}
};

struct dummy_slim_middle_t : public slim_middle_t {
    virtual ~dummy_slim_middle_t() = default;

    void start(const options_t *) { }
    void stop(void) { }
    void cleanup(void) { }
    void analyze(void) { }
    void end(void) { }
    void commit(void) { }

    void nodes_set(osmium::Node const &, double, double, bool) { }
    size_t nodes_get_list(nodelist_t &, const idlist_t) const { return 0; }

    void ways_set(osmium::Way const &, bool) { }
    bool ways_get(osmid_t, taglist_t &, nodelist_t &) const { return true; }
    size_t ways_get_list(const idlist_t &, idlist_t &,
                              std::vector<taglist_t> &,
                              std::vector<nodelist_t> &) const { return 0; }

    void relations_set(osmium::Relation const &, bool) { }
    bool relations_get(osmid_t, memberlist_t &, taglist_t &) const { return 0; }

    void iterate_ways(pending_processor&) { }
    void iterate_relations(pending_processor&) { }

    size_t pending_count() const { return 0; }

    idlist_t relations_using_way(osmid_t) const { return idlist_t(); }

    std::shared_ptr<const middle_query_t> get_instance() const {return std::shared_ptr<const middle_query_t>();}

    void nodes_delete(osmid_t) {};
    void node_changed(osmid_t) {};

    void ways_delete(osmid_t) {};
    void way_changed(osmid_t) {};

    void relations_delete(osmid_t) {};
    void relation_changed(osmid_t) {};
};

struct dummy_output_t : public output_t {

    explicit dummy_output_t(const options_t &options_)
        : output_t(nullptr, options_) {
    }

    virtual ~dummy_output_t() = default;

    int node_add(osmid_t, double, double, const taglist_t &) { return 0; }
    int way_add(osmid_t, const idlist_t &, const taglist_t &) { return 0; }
    int relation_add(osmid_t, const memberlist_t &, const taglist_t &) { return 0; }

    int start() { return 0; }
    int connect(int) { return 0; }
    void stop() { }
    void commit() { }
    void cleanup(void) { }
    void close(int) { }

    void enqueue_ways(pending_queue_t &, osmid_t, size_t, size_t&) { }
    int pending_way(osmid_t, int) { return 0; }

    void enqueue_relations(pending_queue_t &, osmid_t, size_t, size_t&) { }
    int pending_relation(osmid_t, int) { return 0; }

    int node_modify(osmid_t, double, double, const taglist_t &) { return 0; }
    int way_modify(osmid_t, const idlist_t &, const taglist_t &) { return 0; }
    int relation_modify(osmid_t, const memberlist_t &, const taglist_t &) { return 0; }

    int node_delete(osmid_t) { return 0; }
    int way_delete(osmid_t) { return 0; }
    int relation_delete(osmid_t) { return 0; }

};

#endif

