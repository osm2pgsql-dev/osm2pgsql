#ifndef OSM2PGSQL_OUTPUT_FLEX_HPP
#define OSM2PGSQL_OUTPUT_FLEX_HPP

#include "db-copy.hpp"
#include "expire-tiles.hpp"
#include "flex-table-column.hpp"
#include "flex-table.hpp"
#include "format.hpp"
#include "id-tracker.hpp"
#include "osmium-builder.hpp"
#include "output.hpp"
#include "table.hpp"
#include "tagtransform.hpp"

#include <osmium/osm/item_type.hpp>

extern "C"
{
#include <lua.h>
}

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class output_flex_t : public output_t
{
public:
    output_flex_t(std::shared_ptr<middle_query_t> const &mid,
                  options_t const &options,
                  std::shared_ptr<db_copy_thread_t> const &copy_thread,
                  bool is_clone = false,
                  std::shared_ptr<std::string> userdata = nullptr);

    output_flex_t(output_flex_t const &) = delete;
    output_flex_t &operator=(output_flex_t const &) = delete;

    output_flex_t(output_flex_t &&) = delete;
    output_flex_t &operator=(output_flex_t &&) = delete;

    virtual ~output_flex_t() noexcept;

    std::shared_ptr<output_t>
    clone(std::shared_ptr<middle_query_t> const &mid,
          std::shared_ptr<db_copy_thread_t> const &copy_thread) const override;

    void start() override;
    void stop(osmium::thread::Pool *pool) override;
    void commit() override;

    void enqueue_ways(pending_queue_t &job_queue, osmid_t id,
                      std::size_t output_id, std::size_t &added) override;
    void pending_way(osmid_t id, int exists) override;

    void enqueue_relations(pending_queue_t &job_queue, osmid_t id,
                           std::size_t output_id, std::size_t &added) override;
    void pending_relation(osmid_t id, int exists) override;

    void node_add(osmium::Node const &node) override;
    void way_add(osmium::Way *way) override;
    void relation_add(osmium::Relation const &rel) override;

    void node_modify(osmium::Node const &node) override;
    void way_modify(osmium::Way *way) override;
    void relation_modify(osmium::Relation const &rel) override;

    void node_delete(osmid_t id) override;
    void way_delete(osmid_t id) override;
    void relation_delete(osmid_t id) override;

    std::size_t pending_count() const override;

    void merge_pending_relations(output_t *other) override;
    void merge_expire_trees(output_t *other) override;

    int app_define_table();
    int app_mark();
    int app_get_bbox();

    int table_tostring();
    int table_add_row();
    int table_name();
    int table_schema();
    int table_columns();

private:
    void init_clone();

    void call_process_function(int index, osmium::OSMObject const &object);

    std::shared_ptr<std::string> read_userdata() const;

    void init_lua(std::string const &filename,
                  std::shared_ptr<std::string> userdata);

    flex_table_t &create_flex_table();
    void setup_id_columns(flex_table_t *table);
    void setup_flex_table_columns(flex_table_t *table);

    flex_table_t &table_func_params(int n);

    void write_column(db_copy_mgr_t<db_deleter_by_id_t> *copy_mgr,
                      flex_table_column_t const &column);
    void write_row(flex_table_t *table, osmium::item_type id_type, osmid_t id,
                   std::string const &geom);

    void add_row(flex_table_t *table, osmium::Node const &node);
    void add_row(flex_table_t *table, osmium::Way *way);
    void add_row(flex_table_t *table, osmium::Relation const &relation);

    void delete_from_table(flex_table_t *table, osmium::item_type type,
                           osmid_t osm_id);
    void delete_from_tables(osmium::item_type type, osmid_t osm_id);

    std::size_t get_way_nodes();

    std::vector<flex_table_t> m_tables;

    geom::osmium_builder_t m_builder;
    expire_tiles m_expire;

    id_tracker m_ways_pending_tracker;
    id_tracker m_rels_pending_tracker;
    std::shared_ptr<id_tracker> m_ways_done_tracker;

    osmium::memory::Buffer m_buffer;
    osmium::memory::Buffer m_rels_buffer;

    std::shared_ptr<db_copy_thread_t> m_copy_thread;
    lua_State *m_lua_state = nullptr;

    osmium::Node const *m_context_node = nullptr;
    osmium::Way *m_context_way = nullptr;
    osmium::Relation const *m_context_relation = nullptr;

    mutable std::shared_ptr<std::string> m_userdata = nullptr;

    std::size_t m_num_way_nodes = std::numeric_limits<std::size_t>::max();

    bool m_has_process_node = false;
    bool m_has_process_way = false;
    bool m_has_process_relation = false;

    uint8_t m_stage;
};

#endif // OSM2PGSQL_OUTPUT_FLEX_HPP
