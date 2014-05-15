/* Implements dummy output-layer processing for testing.
*/
 
#ifndef OUTPUT_NULL_H
#define OUTPUT_NULL_H

#include "output.hpp"

struct output_null_t : public output_t {
    output_null_t();
    virtual ~output_null_t();

    int start(const struct output_options *options, boost::shared_ptr<reprojection> r);
    int connect(const struct output_options *options, int startTransaction);
    void stop();
    void cleanup(void);
    void close(int stopTransaction);

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
