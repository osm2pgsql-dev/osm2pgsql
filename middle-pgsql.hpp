/* Implements the mid-layer processing for osm2pgsql
 * using several PostgreSQL tables
 *
 * This layer stores data read in from the planet.osm file
 * and is then read by the backend processing code to
 * emit the final geometry-enabled output formats
*/

#ifndef MIDDLE_PGSQL_H
#define MIDDLE_PGSQL_H

#include "middle.hpp"
#include "node-ram-cache.hpp"
#include "node-persistent-cache.hpp"
#include "id-tracker.hpp"
#include <memory>
#include <vector>

struct middle_pgsql_t : public slim_middle_t {
    middle_pgsql_t();
    virtual ~middle_pgsql_t();

    void start(const options_t *out_options_);
    void stop(void);
    void analyze(void);
    void end(void);
    void commit(void);

    void nodes_set(osmium::Node const &node, double lat, double lon,
                   bool extra_tags) override;
    size_t nodes_get_list(nodelist_t &out, osmium::WayNodeList const &nds) const override;
    void nodes_delete(osmid_t id);
    void node_changed(osmid_t id);

    void ways_set(osmium::Way const &way, bool extra_tags) override;
    bool ways_get(osmid_t id, osmium::memory::Buffer &buffer) const override;
    size_t ways_get_list(const idlist_t &ids, osmium::memory::Buffer &buffer) const override;

    void ways_delete(osmid_t id);
    void way_changed(osmid_t id);

    bool relations_get(osmid_t id, osmium::memory::Buffer &buffer) const override;
    void relations_set(osmium::Relation const &rel, bool extra_tags) override;
    void relations_delete(osmid_t id);
    void relation_changed(osmid_t id);

    void iterate_ways(middle_t::pending_processor& pf);
    void iterate_relations(pending_processor& pf);

    size_t pending_count() const;

    idlist_t relations_using_way(osmid_t way_id) const;

    struct table_desc {
        table_desc(const char *name_ = NULL,
                   const char *start_ = NULL,
                   const char *create_ = NULL,
                   const char *create_index_ = NULL,
                   const char *prepare_ = NULL,
                   const char *prepare_intarray_ = NULL,
                   const char *copy_ = NULL,
                   const char *analyze_ = NULL,
                   const char *stop_ = NULL,
                   const char *array_indexes_ = NULL);

        const char *name;
        const char *start;
        const char *create;
        const char *create_index;
        const char *prepare;
        const char *prepare_intarray;
        const char *copy;
        const char *analyze;
        const char *stop;
        const char *array_indexes;

        int copyMode;    /* True if we are in copy mode */
        int transactionMode;    /* True if we are in an extended transaction */
        struct pg_conn *sql_conn;
    };

    virtual std::shared_ptr<const middle_query_t> get_instance() const;
private:
    void pgsql_stop_one(table_desc *table);

    /**
     * Sets up sql_conn for the table
     */
    void connect(table_desc& table);
    void local_nodes_set(osmium::Node const &node,
                         double lat, double lon, bool extra_tags);
    size_t local_nodes_get_list(nodelist_t &out, osmium::WayNodeList const &nds) const;
    void local_nodes_delete(osmid_t osm_id);

    std::vector<table_desc> tables;
    int num_tables;
    table_desc *node_table, *way_table, *rel_table;

    bool append;
    bool mark_pending;

    std::shared_ptr<node_ram_cache> cache;
    std::shared_ptr<node_persistent_cache> persistent_cache;

    std::shared_ptr<id_tracker> ways_pending_tracker, rels_pending_tracker;

    void buffer_store_string(std::string const &in, bool escape);
    void buffer_store_tags(osmium::OSMObject const &obj, bool attrs, bool escape);

    void buffer_correct_params(char const **param, size_t size);

    bool build_indexes;
    std::string copy_buffer;
};

#endif
