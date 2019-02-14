/* Implements the output-layer processing for osm2pgsql
 * storing the data in several PostgreSQL tables
 * with the final PostGIS geometries for each entity
*/

#ifndef OUTPUT_PGSQL_H
#define OUTPUT_PGSQL_H

#include "db-copy.hpp"
#include "expire-tiles.hpp"
#include "id-tracker.hpp"
#include "osmium-builder.hpp"
#include "output.hpp"
#include "table.hpp"
#include "tagtransform.hpp"

#include <array>
#include <memory>

class output_pgsql_t : public output_t {
    output_pgsql_t(output_pgsql_t const *other,
                   std::shared_ptr<middle_query_t> const &mid,
                   std::shared_ptr<db_copy_thread_t> const &copy_thread);

public:
    enum table_id {
        t_point = 0, t_line, t_poly, t_roads, t_MAX
    };

    output_pgsql_t(std::shared_ptr<middle_query_t> const &mid,
                   options_t const &options,
                   std::shared_ptr<db_copy_thread_t> const &copy_thread);
    virtual ~output_pgsql_t();

    std::shared_ptr<output_t>
    clone(std::shared_ptr<middle_query_t> const &mid,
          std::shared_ptr<db_copy_thread_t> const &copy_thread) const override;

    int start() override;
    void stop(osmium::thread::Pool *pool) override;
    void commit() override;

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

    size_t pending_count() const override;

    void merge_pending_relations(output_t *other) override;
    void merge_expire_trees(output_t *other) override;

protected:
    void pgsql_out_way(osmium::Way const &way, taglist_t *tags, bool polygon,
                       bool roads);
    int pgsql_process_relation(osmium::Relation const &rel);
    int pgsql_delete_way_from_output(osmid_t osm_id);
    int pgsql_delete_relation_from_output(osmid_t osm_id);

    std::unique_ptr<tagtransform_t> m_tagtransform;

    //enable output of a generated way_area tag to either hstore or its own column
    int m_enable_way_area;

    std::array<std::unique_ptr<table_t>, t_MAX> m_tables;

    geom::osmium_builder_t m_builder;
    expire_tiles expire;

    id_tracker ways_pending_tracker, rels_pending_tracker;
    std::shared_ptr<id_tracker> ways_done_tracker;
    osmium::memory::Buffer buffer;
    osmium::memory::Buffer rels_buffer;
};

#endif
