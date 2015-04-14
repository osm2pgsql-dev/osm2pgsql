#include "processor-line.hpp"

#include <boost/format.hpp>

processor_line::processor_line(int srid) : geometry_processor(srid, "LINESTRING", interest_way)
{
}

processor_line::~processor_line()
{
}

geometry_builder::maybe_wkt_t processor_line::process_way(const nodelist_t &nodes)
{
    //have the builder make the wkt
    geometry_builder::maybe_wkt_t wkt = builder.get_wkt_simple(nodes, false);
    //hand back the wkt
    return wkt;
}
