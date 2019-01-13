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
class reprojection;

class osmdata_t
{
public:
    osmdata_t(std::shared_ptr<middle_t> mid_,
              std::shared_ptr<output_t> const &out_);
    osmdata_t(std::shared_ptr<middle_t> mid_,
              std::vector<std::shared_ptr<output_t>> const &outs_);

    void start();
    void type_changed(osmium::item_type new_type);
    void stop();

    int node_add(osmium::Node const &node);
    int way_add(osmium::Way *way);
    int relation_add(osmium::Relation const &rel);

    int node_modify(osmium::Node const &node);
    int way_modify(osmium::Way *way);
    int relation_modify(osmium::Relation const &rel);

    int node_delete(osmid_t id);
    int way_delete(osmid_t id);
    int relation_delete(osmid_t id);

private:
    std::shared_ptr<middle_t> mid;
    std::vector<std::shared_ptr<output_t> > outs;
    bool with_extra;
};

#endif
