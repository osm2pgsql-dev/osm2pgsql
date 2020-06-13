#ifndef OSM2PGSQL_OUTPUT_FLEX_HPP
#define OSM2PGSQL_OUTPUT_FLEX_HPP

#include "db-copy.hpp"
#include "expire-tiles.hpp"
#include "flex-table-column.hpp"
#include "flex-table.hpp"
#include "format.hpp"
#include "geom-transform.hpp"
#include "osmium-builder.hpp"
#include "output.hpp"
#include "table.hpp"
#include "tagtransform.hpp"

#include <osmium/index/id_set.hpp>
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

using idset_t = osmium::index::IdSetSmall<osmid_t>;

/**
 * The flex output calls several user-defined Lua functions. They are
 * "prepared" by putting the function pointers on the Lua stack. Objects
 * of the class prepared_lua_function_t are used to hold the stack position
 * of the function which allows them to be called later using a symbolic
 * name.
 */
class prepared_lua_function_t
{
public:
    prepared_lua_function_t() noexcept = default;

    /**
     * Get function with the name "osm2pgsql.name" from Lua and put pointer
     * to it on the Lua stack.
     *
     * \param lua_state Current Lua state.
     * \param name Name of the function.
     * \param nresults The number of results this function is supposed to have.
     */
    prepared_lua_function_t(lua_State *lua_state, const char *name,
                            int nresults = 0);

    /// Return the index of the function on the Lua stack.
    int index() const noexcept { return m_index; }

    /// The name of the function.
    char const* name() const noexcept { return m_name; }

    /// The number of results this function is expected to have.
    int nresults() const noexcept { return m_nresults; }

    /// Is this function defined in the users Lua code?
    operator bool() const noexcept { return m_index != 0; }

private:
    char const *m_name = nullptr;
    int m_index = 0;
    int m_nresults = 0;
};

class output_flex_t : public output_t
{

public:
    output_flex_t(
        std::shared_ptr<middle_query_t> const &mid, options_t const &options,
        std::shared_ptr<db_copy_thread_t> const &copy_thread,
        bool is_clone = false, std::shared_ptr<lua_State> lua_state = nullptr,
        prepared_lua_function_t process_node = {},
        prepared_lua_function_t process_way = {},
        prepared_lua_function_t process_relation = {},
        std::shared_ptr<std::vector<flex_table_t>> tables =
            std::make_shared<std::vector<flex_table_t>>(),
        std::shared_ptr<idset_t> stage2_way_ids = std::make_shared<idset_t>());

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
    void sync() override;

    void stage2_proc() override;

    void pending_way(osmid_t id) override;
    void pending_relation(osmid_t id) override;

    void node_add(osmium::Node const &node) override;
    void way_add(osmium::Way *way) override;
    void relation_add(osmium::Relation const &rel) override;

    void node_modify(osmium::Node const &node) override;
    void way_modify(osmium::Way *way) override;
    void relation_modify(osmium::Relation const &rel) override;

    void node_delete(osmid_t id) override;
    void way_delete(osmid_t id) override;
    void relation_delete(osmid_t id) override;

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

    /**
     * Call a Lua function that was "prepared" earlier with the OSMObject
     * as its only parameter.
     */
    void call_lua_function(prepared_lua_function_t func,
                           osmium::OSMObject const &object);

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

    std::shared_ptr<idset_t> m_stage2_way_ids;

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

    bool m_in_stage2 = false;
    prepared_lua_function_t m_process_node;
    prepared_lua_function_t m_process_way;
    prepared_lua_function_t m_process_relation;
};

#endif // OSM2PGSQL_OUTPUT_FLEX_HPP
