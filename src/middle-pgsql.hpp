#ifndef OSM2PGSQL_MIDDLE_PGSQL_HPP
#define OSM2PGSQL_MIDDLE_PGSQL_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2026 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

/* Implements the mid-layer processing for osm2pgsql
 * using several PostgreSQL tables
 *
 * This layer stores data read in from the planet.osm file
 * and is then read by the backend processing code to
 * emit the final geometry-enabled output formats
*/

#include <map>
#include <memory>

#include <osmium/index/nwr_array.hpp>

#include "db-copy-mgr.hpp"
#include "idlist.hpp"
#include "middle.hpp"
#include "params.hpp"
#include "pgsql.hpp"

class node_locations_t;
class node_persistent_cache_t;

struct middle_pgsql_options
{
    // Store nodes in database.
    bool nodes = false;

    // Store untagged nodes also (set in addition to nodes=true).
    bool untagged_nodes = false;

    // Use a flat node file
    bool use_flat_node_file = false;

    // Store attributes (timestamp, version, changeset id, user id, user name)
    bool with_attributes = false;
};

class middle_query_pgsql_t : public middle_query_t
{
public:
    middle_query_pgsql_t(
        connection_params_t const &connection_params,
        std::shared_ptr<node_locations_t> cache,
        std::shared_ptr<node_persistent_cache_t> persistent_cache,
        middle_pgsql_options const &options);

    osmium::Location get_node_location(osmid_t id) const override;

    size_t nodes_get_list(osmium::WayNodeList *nodes) const override;

    bool node_get(osmid_t id, osmium::memory::Buffer *buffer) const override;

    bool way_get(osmid_t id, osmium::memory::Buffer *buffer) const override;

    size_t rel_members_get(osmium::Relation const &rel,
                           osmium::memory::Buffer *buffer,
                           osmium::osm_entity_bits::type types) const override;

    bool relation_get(osmid_t id,
                      osmium::memory::Buffer *buffer) const override;

    void prepare(std::string const &stmt, std::string const &sql_cmd) const;

private:
    osmium::Location get_node_location_flatnodes(osmid_t id) const;
    osmium::Location get_node_location_db(osmid_t id) const;
    std::size_t get_way_node_locations_flatnodes(osmium::WayNodeList *nodes) const;
    std::size_t get_way_node_locations_db(osmium::WayNodeList *nodes) const;

    pg_conn_t m_db_connection;
    std::shared_ptr<node_locations_t> m_cache;
    std::shared_ptr<node_persistent_cache_t> m_persistent_cache;

    middle_pgsql_options m_store_options;
};

struct middle_pgsql_t : public middle_t
{
    middle_pgsql_t(std::shared_ptr<thread_pool_t> thread_pool,
                   options_t const *options);

    void start() override;
    void stop() override;

    void wait() override;

    void node(osmium::Node const &node) override;
    void way(osmium::Way const &way) override;
    void relation(osmium::Relation const &rel) override;

    void after_nodes() override;
    void after_ways() override;
    void after_relations() override;

    void get_node_parents(idlist_t const &changed_nodes, idlist_t *parent_ways,
                          idlist_t *parent_relations) const override;

    void get_way_parents(idlist_t const &changed_ways,
                         idlist_t *parent_relations) const override;

    class table_desc_t
    {
    public:
        table_desc_t() = default;
        table_desc_t(options_t const &options, std::string_view name);

        std::string const &schema() const noexcept
        {
            return m_copy_target->schema();
        }

        std::string const &name() const noexcept
        {
            return m_copy_target->name();
        }

        std::shared_ptr<db_target_descr_t> const &copy_target() const noexcept
        {
            return m_copy_target;
        }

        ///< Drop table from database using existing database connection.
        void drop_table(pg_conn_t const &db_connection) const;

        void task_set(std::future<std::chrono::microseconds> &&future)
        {
            m_task_result.set(std::move(future));
        }

        std::chrono::microseconds task_wait() { return m_task_result.wait(); }

        void init_max_id(pg_conn_t const &db_connection);

        osmid_t max_id() const noexcept { return m_max_id; }

    private:
        std::shared_ptr<db_target_descr_t> m_copy_target;
        task_result_t m_task_result;

        /// The maximum id in the table (used only in append mode)
        osmid_t m_max_id = 0;
    };

    std::shared_ptr<middle_query_t> get_query_instance() override;

    void set_requirements(output_requirements const &requirements) override;

private:
    void node_set(osmium::Node const &node);
    void node_delete(osmid_t id);

    void way_set(osmium::Way const &way);
    void way_delete(osmid_t id);

    void relation_set(osmium::Relation const &rel);
    void relation_delete(osmid_t id);

    void copy_attributes(osmium::OSMObject const &obj);
    void copy_tags(osmium::OSMObject const &obj);

    void write_users_table();
    void update_users_table();

    std::string render_template(std::string_view templ) const;
    void dbexec(std::string_view templ) const;

    void build_way_node_index();
    void build_relation_member_indexes();

    std::map<osmium::user_id_type, std::string> m_users;
    osmium::nwr_array<table_desc_t> m_tables;

    options_t const *m_options;

    std::shared_ptr<node_locations_t> m_cache;
    std::shared_ptr<node_persistent_cache_t> m_persistent_cache;

    pg_conn_t m_db_connection;

    // middle keeps its own thread for writing to the database.
    std::shared_ptr<db_copy_thread_t> m_copy_thread;
    db_copy_mgr_t<db_deleter_by_id_t> m_db_copy;

    /// Options for this middle.
    middle_pgsql_options m_store_options;

    params_t m_params;

    bool m_append;
};

#endif // OSM2PGSQL_MIDDLE_PGSQL_HPP
