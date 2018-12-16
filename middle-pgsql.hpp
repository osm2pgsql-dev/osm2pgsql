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

#include "id-tracker.hpp"
#include "middle.hpp"
#include "node-persistent-cache.hpp"
#include "node-ram-cache.hpp"
#include "pgsql.hpp"

struct middle_pgsql_t : public slim_middle_t {
    middle_pgsql_t();

    void start(const options_t *out_options_) override;
    void stop(osmium::thread::Pool &pool) override;
    void analyze() override;
    void commit() override;

    void nodes_set(osmium::Node const &node) override;
    size_t nodes_get_list(osmium::WayNodeList *nodes) const override;
    void nodes_delete(osmid_t id) override;
    void node_changed(osmid_t id) override;

    void ways_set(osmium::Way const &way) override;
    bool ways_get(osmid_t id, osmium::memory::Buffer &buffer) const override;
    size_t rel_way_members_get(osmium::Relation const &rel, rolelist_t *roles,
                               osmium::memory::Buffer &buffer) const override;

    void ways_delete(osmid_t id) override;
    void way_changed(osmid_t id) override;

    bool relations_get(osmid_t id, osmium::memory::Buffer &buffer) const override;
    void relations_set(osmium::Relation const &rel) override;
    void relations_delete(osmid_t id) override;
    void relation_changed(osmid_t id) override;

    void flush(osmium::item_type new_type) override;

    void iterate_ways(middle_t::pending_processor& pf) override;
    void iterate_relations(pending_processor& pf) override;

    size_t pending_count() const override;

    idlist_t relations_using_way(osmid_t way_id) const override;

    class table_desc
    {
    public:
        table_desc() : sql_conn(nullptr) {}
        table_desc(char const *name, char const *create,
                   char const *prepare = "", char const *prepare_intarray = "",
                   char const *array_indexes = "");

        ~table_desc();

        char const *name() const { return m_name.c_str(); }
        void clear_array_indexes() { m_array_indexes.clear(); }

        int copyMode;    /* True if we are in copy mode */
        struct pg_conn *sql_conn;

        void connect(options_t const *options);
        void begin();
        void prepare_queries(bool append);
        void create();
        void begin_copy();
        void end_copy();
        pg_result_t
        exec_prepared(char const *stmt, char const *param,
                      ExecStatusType expect = PGRES_TUPLES_OK) const;
        pg_result_t
        exec_prepared(char const *stmt, osmid_t osm_id,
                      ExecStatusType expect = PGRES_TUPLES_OK) const;
        void stop(bool droptemp, bool build_indexes);
        void commit();

    private:
        int transactionMode; /* True if we are in an extended transaction */
        std::string m_name;
        std::string m_create;
        std::string m_prepare;
        std::string m_prepare_intarray;
        std::string m_array_indexes;
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

    /**
     * Sets up sql_conn for the table
     */
    void local_nodes_set(osmium::Node const &node);
    size_t local_nodes_get_list(osmium::WayNodeList *nodes) const;
    void local_nodes_delete(osmid_t osm_id);

    table_desc tables[NUM_TABLES];

    bool append;
    bool mark_pending;

    std::shared_ptr<node_ram_cache> cache;
    std::shared_ptr<node_persistent_cache> persistent_cache;

    std::shared_ptr<id_tracker> ways_pending_tracker, rels_pending_tracker;

    void buffer_store_string(std::string const &in, bool escape);
    void buffer_store_tags(osmium::OSMObject const &obj, bool attrs, bool escape);

    void buffer_correct_params(char const **param, size_t size);

    std::string copy_buffer;
};

#endif
