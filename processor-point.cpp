#include <memory>

#include <boost/format.hpp>

#include "processor-point.hpp"
#include "util.hpp"

processor_point::processor_point(int srid)
    : geometry_processor(srid, "POINT", interest_node) {
}

processor_point::~processor_point() {
}

geometry_builder::maybe_wkt_t processor_point::process_node(double lat, double lon)
{
    using wkt_t = geometry_builder::wkt_t;
    return std::make_shared<wkt_t>((boost::format("POINT(%.15g %.15g)") % lon % lat).str());
}
