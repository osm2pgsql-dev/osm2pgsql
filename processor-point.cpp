#include "processor-point.hpp"
#include "util.hpp"

#include <boost/format.hpp>

processor_point::processor_point(int srid, double scale)
    : geometry_processor(srid, "POINT", interest_node),
      m_scale(scale) {
}

processor_point::~processor_point() {
}

geometry_processor::maybe_wkt_t processor_point::process_node(double lat, double lon) {
#ifdef FIXED_POINT
    // guarantee that we use the same values as in the node cache
    lon = util::fix_to_double(util::double_to_fix(lon, m_scale), m_scale);
    lat = util::fix_to_double(util::double_to_fix(lat, m_scale), m_scale);
#endif

    return (boost::format("POINT(%.15g %.15g)") % lon % lat).str();
}
