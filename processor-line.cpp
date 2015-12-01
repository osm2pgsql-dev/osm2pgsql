#include "processor-line.hpp"

processor_line::processor_line(int srid) : geometry_processor(srid, "LINESTRING", interest_way)
{
}

processor_line::~processor_line()
{
}

geometry_builder::pg_geom_t processor_line::process_way(const nodelist_t &nodes)
{
    return builder.get_wkb_simple(nodes, false);
}
