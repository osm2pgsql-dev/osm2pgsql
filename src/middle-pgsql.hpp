#ifndef OSM2PGSQL_MIDDLE_PGSQL_HPP
#define OSM2PGSQL_MIDDLE_PGSQL_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

/* Implements the mid-layer processing for osm2pgsql
 * using several PostgreSQL tables
 *
 * This layer stores data read in from the planet.osm file
 * and is then read by the backend processing code to
 * emit the final geometry-enabled output formats
*/

#include <memory>

#include <osmium/index/nwr_array.hpp>

#include "db-copy-mgr.hpp"
#include "middle.hpp"
#include "pgsql.hpp"

class node_locations_t;
class node_persistent_cache;
class options_t;

class middle_query_pgsql_t : public middle_query_t
{
public:
    middle_query_pgsql_t(
        std::string const &conninfo, std::shared_ptr<node_locations_t> cache,
        std::shared_ptr<node_persistent_cache> persistent_cache);

    size_t nodes_get_list(osmium::WayNodeList *nodes) const override;

    bool way_get(osmid_t id, osmium::memory::Buffer *buffer) const override;

    size_t rel_members_get(osmium::Relation const &rel,
                           osmium::memory::Buffer *buffer,
                           osmium::osm_entity_bits::type types) const override;

    bool relation_get(osmid_t id,
                      osmium::memory::Buffer *buffer) const override;

    void exec_sql(std::string const &sql_cmd) const;

private:
    std::size_t get_way_node_locations_flatnodes(osmium::WayNodeList *nodes) const;
    std::size_t get_way_node_locations_db(osmium::WayNodeList *nodes) const;

    pg_conn_t m_sql_conn;
    std::shared_ptr<node_locations_t> m_cache;
    std::shared_ptr<node_persistent_cache> m_persistent_cache;
};

struct table_sql {
    char const *name = "";
    char const *create_table = "";
    char const *prepare_query = "";
    char const *prepare_fw_dep_lookups = "";
    char const *create_fw_dep_indexes = "";
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

    idlist_t get_ways_by_node(osmid_t osm_id) override;
    idlist_t get_rels_by_node(osmid_t osm_id) override;
    idlist_t get_rels_by_way(osmid_t osm_id) override;

    class table_desc
    {
    public:
        table_desc() = default;
        table_desc(options_t const &options, table_sql const &ts);

        std::string const &schema() const noexcept
        {
            return m_copy_target->schema;
        }

        std::string const &name() const noexcept { return m_copy_target->name; }

        std::shared_ptr<db_target_descr_t> const &copy_target() const noexcept
        {
            return m_copy_target;
        }

        ///< Drop table from database using existing database connection.
        void drop_table(pg_conn_t const &db_connection) const;

        ///< Open a new database connection and build index on this table.
        void build_index(std::string const &conninfo) const;

        std::string m_create_table;
        std::string m_prepare_query;
        std::string m_prepare_fw_dep_lookups;
        std::string m_create_fw_dep_indexes;

        void task_set(std::future<std::chrono::milliseconds> &&future)
        {
            m_task_result.set(std::move(future));
        }

        std::chrono::milliseconds task_wait() { return m_task_result.wait(); }

    private:
        std::shared_ptr<db_target_descr_t> m_copy_target;
        task_result_t m_task_result;
    };

    std::shared_ptr<middle_query_t> get_query_instance() override;

private:
    void node_set(osmium::Node const &node);
    void node_delete(osmid_t id);

    void way_set(osmium::Way const &way);
    void way_delete(osmid_t id);

    void relation_set(osmium::Relation const &rel);
    void relation_delete(osmid_t id);

    void buffer_store_tags(osmium::OSMObject const &obj, bool attrs);

    osmium::nwr_array<table_desc> m_tables;

    options_t const *m_options;

    std::shared_ptr<node_locations_t> m_cache;
    std::shared_ptr<node_persistent_cache> m_persistent_cache;

    pg_conn_t m_db_connection;

    // middle keeps its own thread for writing to the database.
    std::shared_ptr<db_copy_thread_t> m_copy_thread;
    db_copy_mgr_t<db_deleter_by_id_t> m_db_copy;
};

#endif // OSM2PGSQL_MIDDLE_PGSQL_HPP
