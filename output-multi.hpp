/* One implementation of output-layer processing for osm2pgsql.
 * Manages a single table, transforming geometry using a
 * variety of algorithms plus tag transformation for the
 * database columns.
 */

#ifndef OUTPUT_MULTI_HPP
#define OUTPUT_MULTI_HPP

#include "output.hpp"
#include "tagtransform.hpp"
#include "buffer.hpp"
#include "table.hpp"
#include "geometry-processor.hpp"
#include "id-tracker.hpp"
#include "expire-tiles.hpp"

#include <vector>
#include <boost/scoped_ptr.hpp>
#include <boost/variant.hpp>

class output_multi_t : public output_t {
public:
    output_multi_t(const std::string &name,
                   boost::shared_ptr<geometry_processor> processor_,
                   const struct export_list &export_list_,
                   const middle_query_t* mid_, const options_t &options_);
    output_multi_t(const output_multi_t& other);
    virtual ~output_multi_t();

    virtual boost::shared_ptr<output_t> clone(const middle_query_t* cloned_middle) const;

    int start();
    void stop();
    void commit();

    void enqueue_ways(pending_queue_t &job_queue, osmid_t id, size_t output_id, size_t& added);
    int pending_way(osmid_t id, int exists);

    void enqueue_relations(pending_queue_t &job_queue, osmid_t id, size_t output_id, size_t& added);
    int pending_relation(osmid_t id, int exists);

    int node_add(osmid_t id, double lat, double lon, struct keyval *tags);
    int way_add(osmid_t id, osmid_t *nodes, int node_count, struct keyval *tags);
    int relation_add(osmid_t id, struct member *members, int member_count, struct keyval *tags);

    int node_modify(osmid_t id, double lat, double lon, struct keyval *tags);
    int way_modify(osmid_t id, osmid_t *nodes, int node_count, struct keyval *tags);
    int relation_modify(osmid_t id, struct member *members, int member_count, struct keyval *tags);

    int node_delete(osmid_t id);
    int way_delete(osmid_t id);
    int relation_delete(osmid_t id);

    size_t pending_count() const;

    void merge_pending_relations(boost::shared_ptr<output_t> other);
    void merge_expire_trees(boost::shared_ptr<output_t> other);
    virtual boost::shared_ptr<id_tracker> get_pending_relations();
    virtual boost::shared_ptr<expire_tiles> get_expire_tree();

protected:

    void delete_from_output(osmid_t id);
    int process_node(osmid_t id, double lat, double lon, struct keyval *tags);
    int process_way(osmid_t id, const osmid_t* node_ids, int node_count, struct keyval *tags);
    int reprocess_way(osmid_t id, const osmNode* nodes, int node_count, struct keyval *tags, bool exists);
    int process_relation(osmid_t id, const member *members, int member_count, struct keyval *tags, bool exists, bool pending=false);
    void copy_to_table(osmid_t id, const char *wkt, struct keyval *tags);

    boost::scoped_ptr<tagtransform> m_tagtransform;
    boost::scoped_ptr<export_list> m_export_list;
    boost::shared_ptr<geometry_processor> m_processor;
    const OsmType m_osm_type;
    boost::scoped_ptr<table_t> m_table;
    boost::shared_ptr<id_tracker> ways_pending_tracker, ways_done_tracker, rels_pending_tracker;
    boost::shared_ptr<expire_tiles> m_expire;
    way_helper m_way_helper;
    relation_helper m_relation_helper;

    const static std::string NAME;
};

#endif
