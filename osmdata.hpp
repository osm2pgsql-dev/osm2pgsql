#ifndef OSMDATA_H
#define OSMDATA_H

// when __cplusplus is defined, we need to define this macro as well
// to get the print format specifiers in the inttypes.h header.
#include "config.h"

#include <vector>
#include <memory>

#include "osmtypes.hpp"

class output_t;
struct middle_t;

class osmdata_t {
public:
    osmdata_t(std::shared_ptr<middle_t> mid_, const std::shared_ptr<output_t>& out_);
    osmdata_t(std::shared_ptr<middle_t> mid_, const std::vector<std::shared_ptr<output_t> > &outs_);
    ~osmdata_t();

    void start();
    void stop();

    int node_add(osmid_t id, double lat, double lon, const taglist_t &tags);
    int way_add(osmid_t id, const idlist_t &nodes, const taglist_t &tags);
    int relation_add(osmid_t id, const memberlist_t &members, const taglist_t &tags);

    int node_modify(osmid_t id, double lat, double lon, const taglist_t &tags);
    int way_modify(osmid_t id, const idlist_t &nodes, const taglist_t &tags);
    int relation_modify(osmid_t id, const memberlist_t &members, const taglist_t &tags);

    int node_delete(osmid_t id);
    int way_delete(osmid_t id);
    int relation_delete(osmid_t id);

private:
    std::shared_ptr<middle_t> mid;
    std::vector<std::shared_ptr<output_t> > outs;
};

#endif
