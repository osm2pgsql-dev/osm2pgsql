#ifndef PROCESSOR_POINT_HPP
#define PROCESSOR_POINT_HPP

#include "geometry-processor.hpp"

struct processor_point : public geometry_processor {
    processor_point(int srid);
    virtual ~processor_point();

    geometry_builder::maybe_wkt_t process_node(double lat, double lon);
};

#endif /* PROCESSOR_POINT_HPP */
