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
#include <boost/shared_ptr.hpp>

struct middle_pgsql_t : public slim_middle_t {
    middle_pgsql_t();
    virtual ~middle_pgsql_t() {}

    int start(const options_t *out_options_);
    void stop(void);
    void analyze(void);
    void end(void);
    void commit(void);

    int nodes_set(osmid_t id, double lat, double lon, struct keyval *tags);
    int nodes_get_list(struct osmNode *out, const osmid_t *nds, int nd_count) const;
    int nodes_delete(osmid_t id);
    int node_changed(osmid_t id);

    int ways_set(osmid_t id, osmid_t *nds, int nd_count, struct keyval *tags);
    int ways_get(osmid_t id, struct keyval *tag_ptr, struct osmNode **node_ptr, int *count_ptr) const;
    int ways_get_list(const osmid_t *ids, int way_count, osmid_t *way_ids, struct keyval *tag_ptr, struct osmNode **node_ptr, int *count_ptr) const;

    int ways_delete(osmid_t id);
    int way_changed(osmid_t id);

    int relations_get(osmid_t id, struct member **members, int *member_count, struct keyval *tags) const;
    int relations_set(osmid_t id, struct member *members, int member_count, struct keyval *tags);
    int relations_delete(osmid_t id);
    int relation_changed(osmid_t id);

    void iterate_ways(middle_t::pending_processor& pf);
    void iterate_relations(pending_processor& pf);

    size_t pending_count() const;

    std::vector<osmid_t> relations_using_way(osmid_t way_id) const;

    void *pgsql_stop_one(void *arg);

    struct table_desc {
        table_desc(const char *name_ = "",
                   const char *start_ = "",
                   const char *create_ = "",
                   const char *create_index_ = "",
                   const char *prepare_ = "",
                   const char *prepare_intarray_ = "",
                   const char *copy_ = "",
                   const char *analyze_ = "",
                   const char *stop_ = "",
                   const char *array_indexes_ = "")
        : name(name_),
          start(start_),
          create(create_),
          create_index(create_index_),
          prepare(prepare_),
          prepare_intarray(prepare_intarray_),
          copy(copy_),
          analyze(analyze_),
          stop(stop_),
          array_indexes(array_indexes_),
          copyMode(0),
          transactionMode(0),
          sql_conn(NULL)
        {}

        ~table_desc();

        int connect(const struct options_t *options);

        std::string name;
        std::string start;
        std::string create;
        std::string create_index;
        std::string prepare;
        std::string prepare_intarray;
        std::string copy;
        std::string analyze;
        std::string stop;
        std::string array_indexes;

        int copyMode;    /* True if we are in copy mode */
        int transactionMode;    /* True if we are in an extended transaction */
        struct pg_conn *sql_conn;

        private:
        void set_prefix_and_tbls(const options_t *options, std::string *str);
    };

    virtual boost::shared_ptr<const middle_query_t> get_instance() const;
private:

    int local_nodes_set(const osmid_t& id, const double& lat, const double& lon, const struct keyval *tags);
    int local_nodes_get_list(struct osmNode *nodes, const osmid_t *ndids, const int& nd_count) const;
    int local_nodes_delete(osmid_t osm_id);

    std::vector<table_desc> tables;
    int num_tables;
    struct table_desc *node_table, *way_table, *rel_table;

    int Append;

    boost::shared_ptr<node_ram_cache> cache;
    boost::shared_ptr<node_persistent_cache> persistent_cache;

    boost::shared_ptr<id_tracker> ways_pending_tracker, rels_pending_tracker;

    int build_indexes;
};

#endif
