#ifndef PROCESSOR_POLYGON_HPP
#define PROCESSOR_POLYGON_HPP

#include "geometry-processor.hpp"

struct processor_polygon : public geometry_processor {
    processor_polygon(int srid, bool enable_multi);
    virtual ~processor_polygon();

    geometry_builder::maybe_wkt_t process_way(const nodelist_t &nodes);
    geometry_builder::maybe_wkts_t process_relation(const multinodelist_t &nodes);

private:
    bool enable_multi;
    geometry_builder builder;
};

#endif /* PROCESSOR_POLYGON_HPP */
