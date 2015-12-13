/* Implements the output-layer processing for osm2pgsql
 * storing the data in several PostgreSQL tables
 * with the final PostGIS geometries for each entity
*/

#ifndef OUTPUT_PGSQL_H
#define OUTPUT_PGSQL_H

#include "output.hpp"
#include "tagtransform.hpp"
#include "geometry-builder.hpp"
#include "reprojection.hpp"
#include "expire-tiles.hpp"
#include "id-tracker.hpp"
#include "table.hpp"

#include <vector>
#include <memory>

class output_pgsql_t : public output_t {
public:
    enum table_id {
        t_point = 0, t_line, t_poly, t_roads, t_MAX
    };

    output_pgsql_t(const middle_query_t* mid_, const options_t &options_);
    virtual ~output_pgsql_t();
    output_pgsql_t(const output_pgsql_t& other);

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

    int pgsql_out_node(osmid_t id, const taglist_t &tags, double node_lat, double node_lon);
    int pgsql_out_way(osmid_t id, taglist_t &tags, const nodelist_t &nodes,
                      int polygons, int roads);
    int pgsql_out_relation(osmid_t id, const taglist_t &rel_tags,
                           const multinodelist_t &xnodes, const multitaglist_t & xtags,
                           const idlist_t &xid, const rolelist_t &xrole,
                           bool pending);
    int pgsql_process_relation(osmid_t id, const memberlist_t &members, const taglist_t &tags, int exists, bool pending=false);
    int pgsql_delete_way_from_output(osmid_t osm_id);
    int pgsql_delete_relation_from_output(osmid_t osm_id);

    std::unique_ptr<tagtransform> m_tagtransform;

    //enable output of a generated way_area tag to either hstore or its own column
    int m_enable_way_area;

    std::vector<std::shared_ptr<table_t> > m_tables;

    std::unique_ptr<export_list> m_export_list;

    geometry_builder builder;

    std::shared_ptr<reprojection> reproj;
    std::shared_ptr<expire_tiles> expire;

    std::shared_ptr<id_tracker> ways_pending_tracker, ways_done_tracker, rels_pending_tracker;

    const static std::string NAME;
};

#endif
