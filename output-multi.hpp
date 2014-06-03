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

#include <vector>
#include <boost/scoped_ptr.hpp>

class output_multi_t : public output_t {
public:
    output_multi_t(const std::string &name,
                   boost::shared_ptr<geometry_processor> processor_,
                   struct export_list *export_list_,
                   const middle_query_t* mid_, const options_t &options_);
    virtual ~output_multi_t();

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

    void delete_from_output(osmid_t id);
    int process_node(osmid_t id, double lat, double lon, struct keyval *tags);
    int process_way(osmid_t id, osmid_t *nodes, int node_count, struct keyval *tags);
    int process_relation(osmid_t id, struct member *members, int member_count, struct keyval *tags);
    void copy_to_table(osmid_t id, const char *wkt, struct keyval *tags);

    boost::scoped_ptr<tagtransform> m_tagtransform;
    boost::scoped_ptr<export_list> m_export_list;
    boost::shared_ptr<geometry_processor> m_processor;
    const geometry_processor::interest m_geo_interest;
    boost::scoped_ptr<table_t> m_table;
};

#endif
