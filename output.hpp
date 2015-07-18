/* Common output layer interface */

/* Each output layer must provide methods for
 * storing:
 * - Nodes (Points of interest etc)
 * - Way geometries
 * Associated tags: name, type etc.
*/

#ifndef OUTPUT_H
#define OUTPUT_H

#include "options.hpp"
#include "middle.hpp"
#include "id-tracker.hpp"
#include "expire-tiles.hpp"

#include <boost/noncopyable.hpp>
#include <boost/version.hpp>
#include <utility>
#include <stack>

struct pending_job_t {
    osmid_t osm_id;
    size_t  output_id;

    pending_job_t() : osm_id(0), output_id(0) {}
    pending_job_t(osmid_t id, size_t oid) : osm_id(id), output_id(oid) {}
};

typedef std::stack<pending_job_t> pending_queue_t;

class output_t : public boost::noncopyable {
public:
    static std::vector<std::shared_ptr<output_t> > create_outputs(const middle_query_t *mid, const options_t &options);

    output_t(const middle_query_t *mid, const options_t &options_);
    virtual ~output_t();

    virtual std::shared_ptr<output_t> clone(const middle_query_t* cloned_middle) const = 0;

    virtual int start() = 0;
    virtual void stop() = 0;
    virtual void commit() = 0;

    virtual void enqueue_ways(pending_queue_t &job_queue, osmid_t id, size_t output_id, size_t& added) = 0;
    virtual int pending_way(osmid_t id, int exists) = 0;

    virtual void enqueue_relations(pending_queue_t &job_queue, osmid_t id, size_t output_id, size_t& added) = 0;
    virtual int pending_relation(osmid_t id, int exists) = 0;

    virtual int node_add(osmid_t id, double lat, double lon, const taglist_t &tags) = 0;
    virtual int way_add(osmid_t id, const idlist_t &nodes, const taglist_t &tags) = 0;
    virtual int relation_add(osmid_t id, const memberlist_t &members, const taglist_t &tags) = 0;

    virtual int node_modify(osmid_t id, double lat, double lon, const taglist_t &tags) = 0;
    virtual int way_modify(osmid_t id, const idlist_t &nodes, const taglist_t &tags) = 0;
    virtual int relation_modify(osmid_t id, const memberlist_t &members, const taglist_t &tags) = 0;

    virtual int node_delete(osmid_t id) = 0;
    virtual int way_delete(osmid_t id) = 0;
    virtual int relation_delete(osmid_t id) = 0;

    virtual size_t pending_count() const;

    const options_t *get_options() const;

    virtual void merge_pending_relations(std::shared_ptr<output_t> other);
    virtual void merge_expire_trees(std::shared_ptr<output_t> other);
    virtual std::shared_ptr<id_tracker> get_pending_relations();
    virtual std::shared_ptr<expire_tiles> get_expire_tree();

protected:

    const middle_query_t* m_mid;
    const options_t m_options;
};

#endif
