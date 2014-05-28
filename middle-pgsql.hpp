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
#include <memory>
#include <vector>
#include <boost/shared_ptr.hpp>

struct middle_pgsql_t : public slim_middle_t {
    middle_pgsql_t();
    virtual ~middle_pgsql_t();

    int start(const output_options *out_options_);
    void stop(void);
    void cleanup(void);
    void analyze(void);
    void end(void);
    void commit(void);

    int nodes_set(osmid_t id, double lat, double lon, struct keyval *tags);
    int nodes_get_list(struct osmNode *out, osmid_t *nds, int nd_count);
    int nodes_delete(osmid_t id);
    int node_changed(osmid_t id);

    int ways_set(osmid_t id, osmid_t *nds, int nd_count, struct keyval *tags);
    int ways_get(osmid_t id, struct keyval *tag_ptr, struct osmNode **node_ptr, int *count_ptr);
    int ways_get_list(osmid_t *ids, int way_count, osmid_t **way_ids, struct keyval *tag_ptr, struct osmNode **node_ptr, int *count_ptr);

    int ways_delete(osmid_t id);
    int way_changed(osmid_t id);

    int relations_get(osmid_t id, struct member **members, int *member_count, struct keyval *tags);
    int relations_set(osmid_t id, struct member *members, int member_count, struct keyval *tags);
    int relations_delete(osmid_t id);
    int relation_changed(osmid_t id);

    void iterate_ways(way_cb_func &cb);
    void iterate_relations(rel_cb_func &cb);

    std::vector<osmid_t> relations_using_way(osmid_t way_id);

    void *pgsql_stop_one(void *arg);

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
private:
    int local_nodes_set(const osmid_t& id, const double& lat, const double& lon, const struct keyval *tags);
    int local_nodes_get_list(struct osmNode *nodes, const osmid_t *ndids, const int& nd_count);
    int local_nodes_delete(osmid_t osm_id);

    int ways_done(osmid_t osm_id);
    int relations_done(osmid_t osm_id);

    std::vector<table_desc> tables;
    int num_tables;
    struct table_desc *node_table, *way_table, *rel_table;

    int Append;

    boost::shared_ptr<node_ram_cache> cache;
    boost::shared_ptr<node_persistent_cache> persistent_cache;

    int build_indexes;
};

#endif
