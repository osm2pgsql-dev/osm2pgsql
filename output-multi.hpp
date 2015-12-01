/* One implementation of output-layer processing for osm2pgsql.
 * Manages a single table, transforming geometry using a
 * variety of algorithms plus tag transformation for the
 * database columns.
 */

#ifndef OUTPUT_MULTI_HPP
#define OUTPUT_MULTI_HPP

#include "osmtypes.hpp"
#include "output.hpp"
#include "geometry-processor.hpp"

#include <cstddef>
#include <string>
#include <memory>

class table_t;
class tagtransform;
struct expire_tiles;
struct export_list;
struct id_tracker;
struct middle_query_t;
struct options_t;

class output_multi_t : public output_t {
public:
    output_multi_t(const std::string &name,
                   std::shared_ptr<geometry_processor> processor_,
                   const export_list &export_list_,
                   const middle_query_t* mid_, const options_t &options_);
    output_multi_t(const output_multi_t& other);
    virtual ~output_multi_t();

    virtual std::shared_ptr<output_t> clone(const middle_query_t* cloned_middle) const;

    int start();
    void stop();
    void commit();

    void enqueue_ways(pending_queue_t &job_queue, osmid_t id, size_t output_id, size_t& added);
    int pending_way(osmid_t id, int exists);

    void enqueue_relations(pending_queue_t &job_queue, osmid_t id, size_t output_id, size_t& added);
    int pending_relation(osmid_t id, int exists);

    int node_add(osmid_t id, double lat, double lon, const taglist_t &tags);
    int way_add(osmid_t id, const idlist_t &nodes, const taglist_t &tags);
    int relation_add(osmid_t id, const memberlist_t &members, const taglist_t &tags);

    int node_modify(osmid_t id, double lat, double lon, const taglist_t &tags);
    int way_modify(osmid_t id, const idlist_t &nodes, const taglist_t &tags);
    int relation_modify(osmid_t id, const memberlist_t &members, const taglist_t &tags);

    int node_delete(osmid_t id);
    int way_delete(osmid_t id);
    int relation_delete(osmid_t id);

    size_t pending_count() const;

    void merge_pending_relations(std::shared_ptr<output_t> other);
    void merge_expire_trees(std::shared_ptr<output_t> other);
    virtual std::shared_ptr<id_tracker> get_pending_relations();
    virtual std::shared_ptr<expire_tiles> get_expire_tree();

protected:

    void delete_from_output(osmid_t id);
    int process_node(osmid_t id, double lat, double lon, const taglist_t &tags);
    int process_way(osmid_t id, const idlist_t &nodes, const taglist_t &tags);
    int reprocess_way(osmid_t id, const nodelist_t &nodes, const taglist_t &tags, bool exists);
    int process_relation(osmid_t id, const memberlist_t &members, const taglist_t &tags, bool exists, bool pending=false);
    void copy_node_to_table(osmid_t id, const std::string &geom, const taglist_t &tags);
    void copy_to_table(const osmid_t id, const geometry_builder::pg_geom_t &geom, const taglist_t &tags, int polygon);

    std::unique_ptr<tagtransform> m_tagtransform;
    std::unique_ptr<export_list> m_export_list;
    std::shared_ptr<geometry_processor> m_processor;
    const OsmType m_osm_type;
    std::unique_ptr<table_t> m_table;
    std::shared_ptr<id_tracker> ways_pending_tracker, ways_done_tracker, rels_pending_tracker;
    std::shared_ptr<expire_tiles> m_expire;
    way_helper m_way_helper;
    relation_helper m_relation_helper;

    const static std::string NAME;
};

#endif
