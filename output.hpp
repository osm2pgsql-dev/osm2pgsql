/* Common output layer interface */

/* Each output layer must provide methods for
 * storing:
 * - Nodes (Points of interest etc)
 * - Way geometries
 * Associated tags: name, type etc.
*/

#ifndef OUTPUT_H
#define OUTPUT_H

#include <stack>

#include <osmium/thread/pool.hpp>

#include "options.hpp"

struct expire_tiles;
struct id_tracker;
struct middle_query_t;
class db_copy_thread_t;

struct pending_job_t {
    osmid_t osm_id;
    size_t  output_id;

    pending_job_t() : osm_id(0), output_id(0) {}
    pending_job_t(osmid_t id, size_t oid) : osm_id(id), output_id(oid) {}
};

typedef std::stack<pending_job_t> pending_queue_t;

class output_t
{
public:
    static std::vector<std::shared_ptr<output_t>>
    create_outputs(std::shared_ptr<middle_query_t> const &mid,
                   options_t const &options);

    output_t(std::shared_ptr<middle_query_t> const &mid,
             options_t const &options);
    virtual ~output_t();

    virtual std::shared_ptr<output_t>
    clone(std::shared_ptr<middle_query_t> const &mid,
          std::shared_ptr<db_copy_thread_t> const &copy_thread) const = 0;

    virtual int start() = 0;
    virtual void stop(osmium::thread::Pool *pool) = 0;
    virtual void commit() = 0;

    virtual void enqueue_ways(pending_queue_t &job_queue, osmid_t id, size_t output_id, size_t& added) = 0;
    virtual int pending_way(osmid_t id, int exists) = 0;

    virtual void enqueue_relations(pending_queue_t &job_queue, osmid_t id, size_t output_id, size_t& added) = 0;
    virtual int pending_relation(osmid_t id, int exists) = 0;

    virtual int node_add(osmium::Node const &node) = 0;
    virtual int way_add(osmium::Way *way) = 0;
    virtual int relation_add(osmium::Relation const &rel) = 0;

    virtual int node_modify(osmium::Node const &node) = 0;
    virtual int way_modify(osmium::Way *way) = 0;
    virtual int relation_modify(osmium::Relation const &rel) = 0;

    virtual int node_delete(osmid_t id) = 0;
    virtual int way_delete(osmid_t id) = 0;
    virtual int relation_delete(osmid_t id) = 0;

    virtual size_t pending_count() const;

    const options_t *get_options() const;

    virtual void merge_pending_relations(output_t *other);
    virtual void merge_expire_trees(output_t *other);

protected:
    std::shared_ptr<middle_query_t> m_mid;
    const options_t m_options;
};

#endif
