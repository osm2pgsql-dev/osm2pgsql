/* One implementation of output-layer processing for osm2pgsql.
 * Manages a single table, transforming geometry using a
 * variety of algorithms plus tag transformation for the
 * database columns.
 */
 
#ifndef OUTPUT_MULTI_HPP
#define OUTPUT_MULTI_HPP

#include "output.hpp"
#include "tagtransform.hpp"
#include "buffer.hpp"
#include "table.hpp"
#include "geometry-processor.hpp"
#include "id-tracker.hpp"
#include "expire-tiles.hpp"

#include <vector>
#include <boost/scoped_ptr.hpp>
#include <boost/variant.hpp>

class output_multi_t : public output_t {
public:
    output_multi_t(const std::string &name,
                   boost::shared_ptr<geometry_processor> processor_,
                   const struct export_list &export_list_,
                   const middle_query_t* mid_, const options_t &options_);
    output_multi_t(const output_multi_t& other);
    virtual ~output_multi_t();

    virtual boost::shared_ptr<output_t> clone();

    int start();
    middle_t::cb_func *way_callback();
    middle_t::cb_func *relation_callback();
    void stop();
    void commit();

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
    void delete_from_output(osmid_t id);
    int process_node(osmid_t id, double lat, double lon, struct keyval *tags);
    int process_way(osmid_t id, const osmid_t* node_ids, int node_count, struct keyval *tags);
    int reprocess_way(osmid_t id, const osmNode* nodes, int node_count, struct keyval *tags, bool exists);
    int process_relation(osmid_t id, const member *members, int member_count, struct keyval *tags, bool exists);
    void copy_to_table(osmid_t id, const char *wkt, struct keyval *tags);

    struct way_cb_func : public middle_t::cb_func {
        output_multi_t *m_ptr;
        buffer m_sql;
        osmid_t m_next_internal_id;
        way_cb_func(output_multi_t *ptr);
        virtual ~way_cb_func();
        int operator()(osmid_t id, int exists);
        int do_single(osmid_t id, int exists);
        void finish(int exists);
    };
    struct rel_cb_func : public middle_t::cb_func  {
        output_multi_t *m_ptr;
        buffer m_sql;
        osmid_t m_next_internal_id;
        rel_cb_func(output_multi_t *ptr);
        virtual ~rel_cb_func();
        int operator()(osmid_t id, int exists);
        int do_single(osmid_t id, int exists);
        void finish(int exists);
    };

    friend struct way_cb_func;
    friend struct rel_cb_func;

    boost::scoped_ptr<tagtransform> m_tagtransform;
    boost::scoped_ptr<export_list> m_export_list;
    boost::shared_ptr<geometry_processor> m_processor;
    const OsmType m_osm_type;
    boost::scoped_ptr<table_t> m_table;
    boost::shared_ptr<id_tracker> ways_pending_tracker, ways_done_tracker, rels_pending_tracker;
    boost::shared_ptr<expire_tiles> m_expire;
    way_helper m_way_helper;
    relation_helper m_relation_helper;
};

#endif
