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

    void nodes_set(osmid_t id, double lat, double lon, const taglist_t &tags);
    size_t nodes_get_list(nodelist_t &out, const idlist_t nds) const;
    void nodes_delete(osmid_t id);
    void node_changed(osmid_t id);

    void ways_set(osmid_t id, const idlist_t &nds, const taglist_t &tags);
    bool ways_get(osmid_t id, taglist_t &tags, nodelist_t &nodes) const;
    size_t ways_get_list(const idlist_t &ids, idlist_t &way_ids,
                      multitaglist_t &tags, multinodelist_t &nodes) const;

    void ways_delete(osmid_t id);
    void way_changed(osmid_t id);

    bool relations_get(osmid_t id, memberlist_t &members, taglist_t &tags) const;
    void relations_set(osmid_t id, const memberlist_t &members, const taglist_t &tags);
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
    void local_nodes_set(osmid_t id, double lat, double lon, const taglist_t &tags);
    size_t local_nodes_get_list(nodelist_t &out, const idlist_t nds) const;
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
    void buffer_store_tags(taglist_t const &tags, bool escape);

    void buffer_correct_params(char const **param, size_t size);

    bool build_indexes;
    std::string copy_buffer;
};

#endif
