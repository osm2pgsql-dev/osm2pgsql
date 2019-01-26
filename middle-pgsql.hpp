/* Implements the mid-layer processing for osm2pgsql
 * using several PostgreSQL tables
 *
 * This layer stores data read in from the planet.osm file
 * and is then read by the backend processing code to
 * emit the final geometry-enabled output formats
*/

#ifndef MIDDLE_PGSQL_H
#define MIDDLE_PGSQL_H

#include <memory>

#include "db-copy.hpp"
#include "id-tracker.hpp"
#include "middle.hpp"
#include "node-persistent-cache.hpp"
#include "node-ram-cache.hpp"
#include "pgsql.hpp"

class middle_query_pgsql_t : public middle_query_t
{
public:
    middle_query_pgsql_t(
        char const *conninfo, std::shared_ptr<node_ram_cache> const &cache,
        std::shared_ptr<node_persistent_cache> const &persistent_cache);
    ~middle_query_pgsql_t();

    size_t nodes_get_list(osmium::WayNodeList *nodes) const override;

    bool ways_get(osmid_t id, osmium::memory::Buffer &buffer) const override;
    size_t rel_way_members_get(osmium::Relation const &rel, rolelist_t *roles,
                               osmium::memory::Buffer &buffer) const override;

    idlist_t relations_using_way(osmid_t way_id) const override;
    bool relations_get(osmid_t id,
                       osmium::memory::Buffer &buffer) const override;

    void exec_sql(std::string const &sql_cmd) const;

private:
    size_t local_nodes_get_list(osmium::WayNodeList *nodes) const;

    pg_result_t exec_prepared(char const *stmt, char const *param) const;
    pg_result_t exec_prepared(char const *stmt, osmid_t osm_id) const;

    struct pg_conn *m_sql_conn;
    std::shared_ptr<node_ram_cache> m_cache;
    std::shared_ptr<node_persistent_cache> m_persistent_cache;
};

struct middle_pgsql_t : public slim_middle_t
{
    middle_pgsql_t(options_t const *options);

    void start() override;
    void stop(osmium::thread::Pool &pool) override;
    void analyze() override;
    void commit() override;

    void nodes_set(osmium::Node const &node) override;
    void nodes_delete(osmid_t id) override;
    void node_changed(osmid_t id) override;

    void ways_set(osmium::Way const &way) override;
    void ways_delete(osmid_t id) override;
    void way_changed(osmid_t id) override;

    void relations_set(osmium::Relation const &rel) override;
    void relations_delete(osmid_t id) override;
    void relation_changed(osmid_t id) override;

    void flush(osmium::item_type new_type) override;

    void iterate_ways(middle_t::pending_processor& pf) override;
    void iterate_relations(pending_processor& pf) override;

    size_t pending_count() const override;

    class table_desc
    {
    public:
        table_desc() {}
        table_desc(options_t const *options, char const *name,
                   char const *create, char const *prepare_query,
                   char const *prepare_intarray = "",
                   char const *array_indexes = "");

        char const *name() const { return m_copy_target->name.c_str(); }
        void clear_array_indexes() { m_array_indexes.clear(); }

        void stop(std::string conninfo, bool droptemp, bool build_indexes);

        std::string m_create;
        std::string m_prepare_query;
        std::string m_prepare_intarray;
        std::string m_array_indexes;

        std::shared_ptr<db_target_descr_t> m_copy_target;
    };

    std::shared_ptr<middle_query_t>
    get_query_instance(std::shared_ptr<middle_t> const &mid) const override;

private:
    enum middle_tables
    {
        NODE_TABLE = 0,
        WAY_TABLE,
        REL_TABLE,
        NUM_TABLES
    };

    void buffer_store_tags(osmium::OSMObject const &obj, bool attrs);
    pg_result_t exec_prepared(char const *stmt, osmid_t osm_id) const;

    table_desc tables[NUM_TABLES];

    bool append;
    bool mark_pending;
    options_t const *out_options;

    std::shared_ptr<node_ram_cache> cache;
    std::shared_ptr<node_persistent_cache> persistent_cache;

    std::shared_ptr<id_tracker> ways_pending_tracker, rels_pending_tracker;

    struct pg_conn *m_query_conn;
    // middle keeps its own thread for writing to the database.
    std::shared_ptr<db_copy_thread_t> m_copy_thread;
    db_copy_mgr_t m_db_copy;
};

#endif
