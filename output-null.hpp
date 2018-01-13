/* Implements dummy output-layer processing for testing.
*/

#ifndef OUTPUT_NULL_H
#define OUTPUT_NULL_H

#include "output.hpp"

class output_null_t : public output_t {
public:
    output_null_t(const middle_query_t* mid_, const options_t &options);
    output_null_t(const output_null_t& other);
    virtual ~output_null_t();

    std::shared_ptr<output_t> clone(const middle_query_t* cloned_middle) const override;

    int start() override;
    void stop(osmium::thread::Pool *pool) override;
    void commit() override;
    void cleanup(void);

    void enqueue_ways(pending_queue_t &job_queue, osmid_t id, size_t output_id, size_t& added) override;
    int pending_way(osmid_t id, int exists) override;

    void enqueue_relations(pending_queue_t &job_queue, osmid_t id, size_t output_id, size_t& added) override;
    int pending_relation(osmid_t id, int exists) override;

    int node_add(osmium::Node const &node) override;
    int way_add(osmium::Way *way) override;
    int relation_add(osmium::Relation const &rel) override;

    int node_modify(osmium::Node const &node) override;
    int way_modify(osmium::Way *way) override;
    int relation_modify(osmium::Relation const &rel) override;

    int node_delete(osmid_t id) override;
    int way_delete(osmid_t id) override;
    int relation_delete(osmid_t id) override;
};

#endif
