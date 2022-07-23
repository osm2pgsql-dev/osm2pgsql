#ifndef OSM2PGSQL_OUTPUT_FLEX_HPP
#define OSM2PGSQL_OUTPUT_FLEX_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "expire-tiles.hpp"
#include "flex-table-column.hpp"
#include "flex-table.hpp"
#include "geom.hpp"
#include "output.hpp"

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

class db_copy_thread_t;
class db_deleter_by_type_and_id_t;
class geom_transform_t;
class options_t;
class thread_pool_t;

using idset_t = osmium::index::IdSetSmall<osmid_t>;

/**
 * When C++ code is called from the Lua code we sometimes need to know
 * in what context this happens. These are the possible contexts.
 */
enum class calling_context
{
    main = 0, ///< In main context, i.e. the Lua script outside any callbacks
    process_node = 1, ///< In the process_node() callback
    process_way = 2, ///< In the process_way() callback
    process_relation = 3, ///< In the process_relation() callback
    select_relation_members = 4 ///< In the select_relation_members() callback
};

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
    prepared_lua_function_t(lua_State *lua_state, calling_context context,
                            const char *name, int nresults = 0);

    /// Return the index of the function on the Lua stack.
    int index() const noexcept { return m_index; }

    /// The name of the function.
    char const* name() const noexcept { return m_name; }

    /// The number of results this function is expected to have.
    int nresults() const noexcept { return m_nresults; }

    calling_context context() const noexcept { return m_calling_context; }

    /// Is this function defined in the users Lua code?
    explicit operator bool() const noexcept { return m_index != 0; }

private:
    char const *m_name = nullptr;
    int m_index = 0;
    int m_nresults = 0;
    calling_context m_calling_context = calling_context::main;
};

class output_flex_t : public output_t
{

public:
    output_flex_t(
        std::shared_ptr<middle_query_t> const &mid,
        std::shared_ptr<thread_pool_t> thread_pool, options_t const &options,
        std::shared_ptr<db_copy_thread_t> const &copy_thread,
        bool is_clone = false, std::shared_ptr<lua_State> lua_state = nullptr,
        prepared_lua_function_t process_node = {},
        prepared_lua_function_t process_way = {},
        prepared_lua_function_t process_relation = {},
        prepared_lua_function_t select_relation_members = {},
        std::shared_ptr<std::vector<flex_table_t>> tables =
            std::make_shared<std::vector<flex_table_t>>(),
        std::shared_ptr<idset_t> stage2_way_ids = std::make_shared<idset_t>());

    output_flex_t(output_flex_t const &) = delete;
    output_flex_t &operator=(output_flex_t const &) = delete;

    output_flex_t(output_flex_t &&) = delete;
    output_flex_t &operator=(output_flex_t &&) = delete;

    ~output_flex_t() noexcept override = default;

    std::shared_ptr<output_t>
    clone(std::shared_ptr<middle_query_t> const &mid,
          std::shared_ptr<db_copy_thread_t> const &copy_thread) const override;

    void start() override;
    void stop() override;
    void sync() override;

    void wait() override;

    idset_t const &get_marked_way_ids() override;
    void reprocess_marked() override;

    void pending_way(osmid_t id) override;
    void pending_relation(osmid_t id) override;
    void pending_relation_stage1c(osmid_t id) override;

    void select_relation_members(osmid_t id) override;

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
    int app_mark_way();
    int app_get_bbox();

    int table_tostring();
    int table_add_row();
    int table_name();
    int table_schema();
    int table_cluster();
    int table_columns();

private:
    void init_clone();
    void select_relation_members();

    /**
     * Call a Lua function that was "prepared" earlier with the OSMObject
     * as its only parameter.
     */
    void call_lua_function(prepared_lua_function_t func,
                           osmium::OSMObject const &object);

    /// Aquire the lua_mutex and the call `call_lua_function()`.
    void get_mutex_and_call_lua_function(prepared_lua_function_t func,
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
                   geom::geometry_t const &geom, int srid);

    geom::geometry_t run_transform(reprojection const &proj,
                                   geom_transform_t const *transform,
                                   osmium::Node const &node);

    geom::geometry_t run_transform(reprojection const &proj,
                                   geom_transform_t const *transform,
                                   osmium::Way const &way);

    geom::geometry_t run_transform(reprojection const &proj,
                                   geom_transform_t const *transform,
                                   osmium::Relation const &relation);

    template <typename OBJECT>
    void add_row(table_connection_t *table_connection, OBJECT const &object);

    void delete_from_table(table_connection_t *table_connection,
                           osmium::item_type type, osmid_t osm_id);
    void delete_from_tables(osmium::item_type type, osmid_t osm_id);

    lua_State *lua_state() noexcept { return m_lua_state.get(); }

    void remember_memory_used_by_lua() noexcept;

    class way_cache_t
    {
    public:
        bool init(middle_query_t const &middle, osmid_t id);
        void init(osmium::Way *way);
        std::size_t add_nodes(middle_query_t const &middle);
        osmium::Way const &get() const noexcept { return *m_way; }

    private:
        osmium::memory::Buffer m_buffer{32768,
                                        osmium::memory::Buffer::auto_grow::yes};
        osmium::Way *m_way = nullptr;
        std::size_t m_num_way_nodes = std::numeric_limits<std::size_t>::max();

    }; // way_cache_t

    class relation_cache_t
    {
    public:
        bool init(middle_query_t const &middle, osmid_t id);
        void init(osmium::Relation const &relation);

        /**
         * Add members of relation to cache when it is first called.
         *
         * \returns True if at least one member was found, false otherwise
         */
        bool add_members(middle_query_t const &middle);

        osmium::Relation const &get() const noexcept
        {
            return *m_relation;
        }

        osmium::memory::Buffer const &members_buffer() const noexcept
        {
            return m_members_buffer;
        }

    private:
        // This buffer is used for the relation only. Members are stored in
        // m_members_buffer. If we would store more objects in this buffer,
        // it might autogrow which would invalidate the m_relation pointer.
        osmium::memory::Buffer m_relation_buffer{
            1024, osmium::memory::Buffer::auto_grow::yes};
        osmium::memory::Buffer m_members_buffer{
            32768, osmium::memory::Buffer::auto_grow::yes};
        osmium::Relation const *m_relation = nullptr;

    }; // relation_cache_t

    std::shared_ptr<std::vector<flex_table_t>> m_tables;
    std::vector<table_connection_t> m_table_connections;

    // This is shared between all clones of the output and must only be
    // accessed while protected using the lua_mutex.
    std::shared_ptr<idset_t> m_stage2_way_ids;

    std::shared_ptr<db_copy_thread_t> m_copy_thread;

    // This is shared between all clones of the output and must only be
    // accessed while protected using the lua_mutex.
    std::shared_ptr<lua_State> m_lua_state;

    expire_tiles m_expire;

    way_cache_t m_way_cache;
    relation_cache_t m_relation_cache;
    osmium::Node const *m_context_node = nullptr;

    prepared_lua_function_t m_process_node;
    prepared_lua_function_t m_process_way;
    prepared_lua_function_t m_process_relation;
    prepared_lua_function_t m_select_relation_members;

    calling_context m_calling_context = calling_context::main;

    std::size_t m_lua_memory_counter = 0;
    int m_lua_memory_max = 0;

    /**
     * This is set before calling stage1c process_relation() to disable the
     * add_row() command.
     */
    bool m_disable_add_row = false;
};

#endif // OSM2PGSQL_OUTPUT_FLEX_HPP
