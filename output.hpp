/* Common output layer interface */

/* Each output layer must provide methods for 
 * storing:
 * - Nodes (Points of interest etc)
 * - Way geometries
 * Associated tags: name, type etc. 
*/

#ifndef OUTPUT_H
#define OUTPUT_H

#include "middle.hpp"
#include "id-tracker.hpp"
#include "expire-tiles.hpp"

#include <boost/noncopyable.hpp>
#include <boost/version.hpp>

typedef std::pair<osmid_t, size_t> pending_job_t;
#if BOOST_VERSION < 105300
#include <stack>
typedef std::stack<pending_job_t> pending_queue_t;
#else
#include <boost/lockfree/queue.hpp>
typedef boost::lockfree::queue<pending_job_t> pending_queue_t;
#endif

class output_t : public boost::noncopyable {
public:
    static std::vector<boost::shared_ptr<output_t> > create_outputs(const middle_query_t *mid, const options_t &options);

    output_t(const middle_query_t *mid, const options_t &options_);
    virtual ~output_t();

    virtual boost::shared_ptr<output_t> clone(const middle_query_t* cloned_middle) const = 0;

    virtual int start() = 0;
    virtual void stop() = 0;
    virtual void commit() = 0;

    virtual void enqueue_ways(pending_queue_t &job_queue, osmid_t id, size_t output_id, size_t& added) = 0;
    virtual int pending_way(osmid_t id, int exists) = 0;

    virtual void enqueue_relations(pending_queue_t &job_queue, osmid_t id, size_t output_id, size_t& added) = 0;
    virtual int pending_relation(osmid_t id, int exists) = 0;

    virtual int node_add(osmid_t id, double lat, double lon, struct keyval *tags) = 0;
    virtual int way_add(osmid_t id, osmid_t *nodes, int node_count, struct keyval *tags) = 0;
    virtual int relation_add(osmid_t id, struct member *members, int member_count, struct keyval *tags) = 0;

    virtual int node_modify(osmid_t id, double lat, double lon, struct keyval *tags) = 0;
    virtual int way_modify(osmid_t id, osmid_t *nodes, int node_count, struct keyval *tags) = 0;
    virtual int relation_modify(osmid_t id, struct member *members, int member_count, struct keyval *tags) = 0;

    virtual int node_delete(osmid_t id) = 0;
    virtual int way_delete(osmid_t id) = 0;
    virtual int relation_delete(osmid_t id) = 0;

    virtual size_t pending_count() const;

    const options_t *get_options() const;

    virtual void merge_pending_relations(boost::shared_ptr<output_t> other);
    virtual void merge_expire_trees(boost::shared_ptr<output_t> other);
    virtual boost::shared_ptr<id_tracker> get_pending_relations();
    virtual boost::shared_ptr<expire_tiles> get_expire_tree();

protected:

    const middle_query_t* m_mid;
    const options_t m_options;
};

unsigned int pgsql_filter_tags(enum OsmType type, struct keyval *tags, int *polygon);

#endif
