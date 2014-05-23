/* Implements dummy output-layer processing for testing.
*/
 
#ifndef OUTPUT_NULL_H
#define OUTPUT_NULL_H

#include "output.hpp"

class output_null_t : public output_t {
public:
    output_null_t(middle_t* mid_, const output_options* options);
    virtual ~output_null_t();

    int start();
    int connect(int startTransaction);
    void pre_stop();
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
