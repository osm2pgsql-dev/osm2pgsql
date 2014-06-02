/* Implements the output-layer processing for osm2pgsql
 * storing the data in several PostgreSQL tables
 * with the final PostGIS geometries for each entity
*/
 
#ifndef OUTPUT_PGSQL_H
#define OUTPUT_PGSQL_H

#include "output.hpp"
#include "tagtransform.hpp"
#include "buffer.hpp"
#include "build_geometry.hpp"
#include "reprojection.hpp"
#include "expire-tiles.hpp"
#include "pgsql-id-tracker.hpp"
#include "table.hpp"

#include <vector>
#include <boost/shared_ptr.hpp>

#define FLAG_POLYGON 1    /* For polygon table */
#define FLAG_LINEAR  2    /* For lines table */
#define FLAG_NOCACHE 4    /* Optimisation: don't bother remembering this one */
#define FLAG_DELETE  8    /* These tags should be simply deleted on sight */
#define FLAG_PHSTORE 17   /* polygons without own column but listed in hstore this implies FLAG_POLYGON */

class output_pgsql_t : public output_t {
public:
    enum table_id {
        t_point = 0, t_line, t_poly, t_roads, t_MAX
    };
    
    output_pgsql_t(const middle_query_t* mid_, const options_t &options_);
    virtual ~output_pgsql_t();

    int start();
    middle_t::way_cb_func *way_callback();
    middle_t::rel_cb_func *relation_callback();
    void stop();
    void commit();
    void cleanup();

    int node_add(osmid_t id, double lat, double lon, struct keyval *tags);
    int way_add(osmid_t id, osmid_t *nodes, int node_count, struct keyval *tags);
    int relation_add(osmid_t id, struct member *members, int member_count, struct keyval *tags);

    int node_modify(osmid_t id, double lat, double lon, struct keyval *tags);
    int way_modify(osmid_t id, osmid_t *nodes, int node_count, struct keyval *tags);
    int relation_modify(osmid_t id, struct member *members, int member_count, struct keyval *tags);

    int node_delete(osmid_t id);
    int way_delete(osmid_t id);
    int relation_delete(osmid_t id);

private:

    struct way_cb_func : public middle_t::way_cb_func {
        output_pgsql_t *m_ptr;
        buffer m_sql;
        osmid_t m_next_internal_id;
        way_cb_func(output_pgsql_t *ptr);
        virtual ~way_cb_func();
        int operator()(osmid_t id, struct keyval *tags, struct osmNode *nodes, int count, int exists);
        void finish(int exists);
        void run_internal_until(osmid_t id, int exists);
    };
    struct rel_cb_func : public middle_t::rel_cb_func  {
        output_pgsql_t *m_ptr;
        buffer m_sql;
        osmid_t m_next_internal_id;
        rel_cb_func(output_pgsql_t *ptr);
        virtual ~rel_cb_func();
        int operator()(osmid_t id, struct member *, int member_count, struct keyval *rel_tags, int exists);
        void finish(int exists);
        void run_internal_until(osmid_t id, int exists);
    };

    friend struct way_cb_func;
    friend struct rel_cb_func;
    
    int pgsql_out_node(osmid_t id, struct keyval *tags, double node_lat, double node_lon, buffer &sql);
    int pgsql_out_way(osmid_t id, struct keyval *tags, struct osmNode *nodes, int count, int exists, buffer &sql);
    int pgsql_out_relation(osmid_t id, struct keyval *rel_tags, int member_count, struct osmNode **xnodes, struct keyval *xtags, int *xcount, osmid_t *xid, const char **xrole, buffer &sql);
    int pgsql_process_relation(osmid_t id, struct member *members, int member_count, struct keyval *tags, int exists, buffer &sql);
    int pgsql_delete_way_from_output(osmid_t osm_id);
    int pgsql_delete_relation_from_output(osmid_t osm_id);

    tagtransform *m_tagtransform;

    //enable output of a generated way_area tag to either hstore or its own column
    int m_enable_way_area;

    std::vector<boost::shared_ptr<table_t> > m_tables;
    
    export_list *m_export_list;

    buffer m_sql;

    build_geometry builder;

    boost::shared_ptr<reprojection> reproj;
    boost::shared_ptr<expire_tiles> expire;

    boost::shared_ptr<pgsql_id_tracker> ways_pending_tracker, ways_done_tracker, rels_pending_tracker;
};

#endif
