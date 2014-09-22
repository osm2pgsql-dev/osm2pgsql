/* Implements dummy output-layer processing for testing.
*/
 
#ifndef OUTPUT_NULL_H
#define OUTPUT_NULL_H

#include "output.hpp"

class output_null_t : public output_t {
public:
    output_null_t(const middle_query_t* mid_, const options_t &options);
    output_null_t(const output_null_t& other);
    virtual ~output_null_t();

    virtual boost::shared_ptr<output_t> clone(const middle_query_t* cloned_middle);

    int start();
    middle_t::cb_func *way_callback();
    middle_t::cb_func *relation_callback();
    void stop();
    void commit();
    void cleanup(void);

    int node_add(osmid_t id, double lat, double lon, struct keyval *tags);
    int way_add(osmid_t id, osmid_t *nodes, int node_count, struct keyval *tags);
    int relation_add(osmid_t id, struct member *members, int member_count, struct keyval *tags);

    int node_modify(osmid_t id, double lat, double lon, struct keyval *tags);
    int way_modify(osmid_t id, osmid_t *nodes, int node_count, struct keyval *tags);
    int relation_modify(osmid_t id, struct member *members, int member_count, struct keyval *tags);

    int node_delete(osmid_t id);
    int way_delete(osmid_t id);
    int relation_delete(osmid_t id);
};

extern output_null_t out_null;

#endif
