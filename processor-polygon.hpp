#ifndef PROCESSOR_POLYGON_HPP
#define PROCESSOR_POLYGON_HPP

#include "geometry-processor.hpp"

struct processor_polygon : public geometry_processor {
    processor_polygon(int srid, bool enable_multi);
    virtual ~processor_polygon();

    geometry_builder::pg_geom_t process_way(const nodelist_t &nodes);
    geometry_builder::pg_geoms_t process_relation(const multinodelist_t &nodes);

private:
    bool enable_multi;
    geometry_builder builder;
};

#endif /* PROCESSOR_POLYGON_HPP */
