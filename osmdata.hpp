#ifndef OSMDATA_H
#define OSMDATA_H

// when __cplusplus is defined, we need to define this macro as well
// to get the print format specifiers in the inttypes.h header.
#include <config.h>
#include <vector>

#include "output.hpp"

class osmdata_t {
public:
    osmdata_t(boost::shared_ptr<middle_t> mid_, const boost::shared_ptr<output_t>& out_);
    osmdata_t(boost::shared_ptr<middle_t> mid_, const std::vector<boost::shared_ptr<output_t> > &outs_);
    ~osmdata_t();

    void start();
    void stop();

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
    boost::shared_ptr<middle_t> mid;
    std::vector<boost::shared_ptr<output_t> > outs;
};

#endif
