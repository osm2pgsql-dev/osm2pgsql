#include <memory>

#include <boost/format.hpp>

#include "processor-point.hpp"
#include "util.hpp"

processor_point::processor_point(int srid)
    : geometry_processor(srid, "POINT", interest_node) {
}

processor_point::~processor_point() {
}

geometry_builder::pg_geom_t processor_point::process_node(double lat, double lon)
{
    return geometry_builder::pg_geom_t((boost::format("POINT(%.15g %.15g)") % lon % lat).str(), false);
}
