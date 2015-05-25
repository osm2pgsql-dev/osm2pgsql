#include "processor-point.hpp"
#include "util.hpp"

#include <boost/format.hpp>

processor_point::processor_point(int srid, double scale)
    : geometry_processor(srid, "POINT", interest_node),
      m_scale(scale) {
}

processor_point::~processor_point() {
}

geometry_builder::maybe_wkt_t processor_point::process_node(double lat, double lon) {
    // guarantee that we use the same values as in the node cache
    lon = util::fix_to_double(util::double_to_fix(lon, m_scale), m_scale);
    lat = util::fix_to_double(util::double_to_fix(lat, m_scale), m_scale);
    geometry_builder::maybe_wkt_t wkt(new geometry_builder::wkt_t());
    wkt->geom = (boost::format("POINT(%.15g %.15g)") % lon % lat).str();
    return wkt;
}
