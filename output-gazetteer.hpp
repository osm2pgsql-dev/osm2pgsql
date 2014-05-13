#ifndef OUTPUT_GAZETTEER_H
#define OUTPUT_GAZETTEER_H

#include "output.hpp"

struct output_gazetteer_t : public output_t {
    output_gazetteer_t();
    virtual ~output_gazetteer_t();

    int start(const struct output_options *options);
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

extern output_gazetteer_t out_gazetteer;

#endif
