#ifndef OSM2PGSQL_MIDDLE_PGSQL_HPP
#define OSM2PGSQL_MIDDLE_PGSQL_HPP

/* Implements the mid-layer processing for osm2pgsql
 * using several PostgreSQL tables
 *
 * This layer stores data read in from the planet.osm file
 * and is then read by the backend processing code to
 * emit the final geometry-enabled output formats
*/

#include <memory>

#include "db-copy-mgr.hpp"
#include "middle.hpp"
#include "node-persistent-cache.hpp"
#include "node-ram-cache.hpp"
#include "pgsql.hpp"

class middle_query_pgsql_t : public middle_query_t
{
public:
    middle_query_pgsql_t(
        std::string const &conninfo,
        std::shared_ptr<node_ram_cache> const &cache,
        std::shared_ptr<node_persistent_cache> const &persistent_cache);

    size_t nodes_get_list(osmium::WayNodeList *nodes) const override;

    bool way_get(osmid_t id, osmium::memory::Buffer &buffer) const override;
    size_t rel_way_members_get(osmium::Relation const &rel, rolelist_t *roles,
                               osmium::memory::Buffer &buffer) const override;

    bool relation_get(osmid_t id,
                      osmium::memory::Buffer &buffer) const override;

    void exec_sql(std::string const &sql_cmd) const;

private:
    size_t local_nodes_get_list(osmium::WayNodeList *nodes) const;

    pg_conn_t m_sql_conn;
    std::shared_ptr<node_ram_cache> m_cache;
    std::shared_ptr<node_persistent_cache> m_persistent_cache;
};

struct table_sql {
    char const *name = "";
    char const *create_table = "";
    char const *prepare_query = "";
    char const *prepare_fw_dep_lookups = "";
    char const *create_fw_dep_indexes = "";
};

struct middle_pgsql_t : public slim_middle_t
{
    middle_pgsql_t(options_t const *options);

    void start() override;
    void stop(thread_pool_t &pool) override;
    void analyze() override;
    void commit() override;

    void node_set(osmium::Node const &node) override;
    void node_delete(osmid_t id) override;

    void way_set(osmium::Way const &way) override;
    void way_delete(osmid_t id) override;

    void relation_set(osmium::Relation const &rel) override;
    void relation_delete(osmid_t id) override;

    void flush() override;

    idlist_t get_ways_by_node(osmid_t osm_id) override;
    idlist_t get_rels_by_node(osmid_t osm_id) override;
    idlist_t get_rels_by_way(osmid_t osm_id) override;

    class table_desc
    {
    public:
        table_desc() {}
        table_desc(options_t const &options, table_sql const &ts);

        char const *name() const { return m_copy_target->name.c_str(); }

        void stop(std::string const &conninfo, bool droptemp,
                  bool build_indexes);

        std::string m_create_table;
        std::string m_prepare_query;
        std::string m_prepare_fw_dep_lookups;
        std::string m_create_fw_dep_indexes;

        std::shared_ptr<db_target_descr_t> m_copy_target;
    };

    std::shared_ptr<middle_query_t> get_query_instance() override;

private:
    enum middle_tables
    {
        NODE_TABLE = 0,
        WAY_TABLE,
        REL_TABLE,
        NUM_TABLES
    };

    void buffer_store_tags(osmium::OSMObject const &obj, bool attrs);

    idlist_t get_ids(const char* stmt, osmid_t osm_id);

    table_desc m_tables[NUM_TABLES];

    bool m_append;
    options_t const *m_out_options;

    std::shared_ptr<node_ram_cache> m_cache;
    std::shared_ptr<node_persistent_cache> m_persistent_cache;

    pg_conn_t m_db_connection;

    // middle keeps its own thread for writing to the database.
    std::shared_ptr<db_copy_thread_t> m_copy_thread;
    db_copy_mgr_t<db_deleter_by_id_t> m_db_copy;
};

#endif // OSM2PGSQL_MIDDLE_PGSQL_HPP
