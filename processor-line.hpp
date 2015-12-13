#ifndef PROCESSOR_LINE_HPP
#define PROCESSOR_LINE_HPP

#include "geometry-processor.hpp"

struct processor_line : public geometry_processor {
    processor_line(int srid);
    virtual ~processor_line();

    geometry_builder::pg_geom_t process_way(const nodelist_t &nodes);

private:
    geometry_builder builder;
};

#endif /* PROCESSOR_LINE_HPP */
