#ifndef OSM2PGSQL_OUTPUT_FLEX_HPP
#define OSM2PGSQL_OUTPUT_FLEX_HPP

#include "db-copy.hpp"
#include "expire-tiles.hpp"
#include "flex-table-column.hpp"
#include "flex-table.hpp"
#include "format.hpp"
#include "geom-transform.hpp"
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
                  std::shared_ptr<lua_State> lua_state = nullptr,
                  bool has_process_node = false, bool has_process_way = false,
                  bool has_process_relation = false,
                  std::shared_ptr<std::vector<flex_table_t>> tables =
                      std::make_shared<std::vector<flex_table_t>>(),
                  std::shared_ptr<id_tracker> ways_tracker_1c =
                      std::make_shared<id_tracker>(),
                  std::shared_ptr<id_tracker> ways_tracker_2 =
                      std::make_shared<id_tracker>(),
                  std::shared_ptr<id_tracker> rels_tracker_2 =
                      std::make_shared<id_tracker>());

    output_flex_t(output_flex_t const &) = delete;
    output_flex_t &operator=(output_flex_t const &) = delete;

    output_flex_t(output_flex_t &&) = delete;
    output_flex_t &operator=(output_flex_t &&) = delete;

    virtual ~output_flex_t() noexcept = default;

    std::shared_ptr<output_t>
    clone(std::shared_ptr<middle_query_t> const &mid,
          std::shared_ptr<db_copy_thread_t> const &copy_thread) const override;

    void start() override;
    void stop(osmium::thread::Pool *pool) override;
    void commit() override;

    void stage1c_proc(slim_middle_t *) override;
    void stage2_proc() override;

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

    bool has_pending() const override;
    bool has_stage1c_pending() const override;

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

    void init_lua(std::string const &filename);

    flex_table_t &create_flex_table();
    void setup_id_columns(flex_table_t *table);
    void setup_flex_table_columns(flex_table_t *table);

    flex_table_t const &get_table_from_param();

    void write_column(db_copy_mgr_t<db_deleter_by_type_and_id_t> *copy_mgr,
                      flex_table_column_t const &column);
    void write_row(table_connection_t *table_connection,
                   osmium::item_type id_type, osmid_t id,
                   std::string const &geom);

    geom::osmium_builder_t::wkbs_t
    run_transform(geom_transform_t const *transform, osmium::Node const &node);

    geom::osmium_builder_t::wkbs_t
    run_transform(geom_transform_t const *transform, osmium::Way const &way);

    geom::osmium_builder_t::wkbs_t
    run_transform(geom_transform_t const *transform,
                  osmium::Relation const &relation);

    template <typename OBJECT>
    void add_row(table_connection_t *table_connection, OBJECT const &object);

    void delete_from_table(table_connection_t *table_connection,
                           osmium::item_type type, osmid_t osm_id);
    void delete_from_tables(osmium::item_type type, osmid_t osm_id);

    std::size_t get_way_nodes();

    lua_State *lua_state() noexcept { return m_lua_state.get(); }

    std::shared_ptr<std::vector<flex_table_t>> m_tables;
    std::vector<table_connection_t> m_table_connections;

    id_tracker m_ways_pending_tracker;
    id_tracker m_rels_pending_tracker;
    std::shared_ptr<id_tracker> m_ways_done_tracker;

    std::shared_ptr<id_tracker> m_stage1c_ways_tracker;
    std::shared_ptr<id_tracker> m_stage2_ways_tracker;
    std::shared_ptr<id_tracker> m_stage2_rels_tracker;

    std::shared_ptr<db_copy_thread_t> m_copy_thread;

    std::shared_ptr<lua_State> m_lua_state;

    geom::osmium_builder_t m_builder;
    expire_tiles m_expire;

    osmium::memory::Buffer m_buffer;
    osmium::memory::Buffer m_rels_buffer;

    osmium::Node const *m_context_node = nullptr;
    osmium::Way *m_context_way = nullptr;
    osmium::Relation const *m_context_relation = nullptr;

    std::size_t m_num_way_nodes = std::numeric_limits<std::size_t>::max();

    enum class stage : int8_t {
        stage1a,
        stage1b,
        stage1c,
        stage2,
    };

    stage m_stage = stage::stage1a;
    bool m_has_process_node = false;
    bool m_has_process_way = false;
    bool m_has_process_relation = false;
};

#endif // OSM2PGSQL_OUTPUT_FLEX_HPP
