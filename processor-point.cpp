#include "config.h" // for FIXED_POINT
#include "processor-point.hpp"
#include "util.hpp"

#include <boost/format.hpp>

processor_point::processor_point(int srid)
    : geometry_processor(srid, "POINT", interest_node) {
}

processor_point::~processor_point() {
}

geometry_builder::maybe_wkt_t processor_point::process_node(double lat, double lon) {
    geometry_builder::maybe_wkt_t wkt(new geometry_builder::wkt_t());
    wkt->geom = (boost::format("POINT(%.15g %.15g)") % lon % lat).str();
    return wkt;
}
