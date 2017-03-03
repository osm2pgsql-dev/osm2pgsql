#include <memory>

#include <boost/format.hpp>

#include "processor-point.hpp"
#include "util.hpp"

processor_point::processor_point(std::shared_ptr<reprojection> const &proj)
: geometry_processor(proj->target_srs(), "POINT", interest_node),
  m_builder(proj, false)
{
}

geometry_processor::wkb_t
processor_point::process_node(osmium::Location const &loc)
{
    return m_builder.get_wkb_node(loc);
}
