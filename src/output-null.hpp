#ifndef OSM2PGSQL_OUTPUT_NULL_HPP
#define OSM2PGSQL_OUTPUT_NULL_HPP

/* Implements dummy output-layer processing for testing.
*/

#include "output.hpp"

class output_null_t : public output_t
{
public:
    output_null_t(std::shared_ptr<middle_query_t> const &mid,
                  options_t const &options);
    virtual ~output_null_t();

    std::shared_ptr<output_t>
    clone(std::shared_ptr<middle_query_t> const &mid,
          std::shared_ptr<db_copy_thread_t> const &copy_thread) const override;

    void start() override {}
    void stop(osmium::thread::Pool * /*pool*/) override {}
    void commit() override {}
    void cleanup() {}

    void enqueue_ways(pending_queue_t & /*job_queue*/, osmid_t /*id*/, size_t /*output_id*/,
                      size_t & /*added*/) override
    {}

    void pending_way(osmid_t /*id*/, int /*exists*/) override {}

    void enqueue_relations(pending_queue_t & /*job_queue*/, osmid_t /*id*/,
                           size_t /*output_id*/, size_t & /*added*/) override
    {}

    void pending_relation(osmid_t /*id*/, int /*exists*/) override {}

    void node_add(osmium::Node const & /*node*/) override {}
    void way_add(osmium::Way * /*way*/) override {}
    void relation_add(osmium::Relation const & /*rel*/) override {}

    void node_modify(osmium::Node const & /*node*/) override {}
    void way_modify(osmium::Way * /*way*/) override {}
    void relation_modify(osmium::Relation const & /*rel*/) override {}

    void node_delete(osmid_t /*id*/) override {}
    void way_delete(osmid_t /*id*/) override {}
    void relation_delete(osmid_t /*id*/) override {}
};

#endif // OSM2PGSQL_OUTPUT_NULL_HPP
