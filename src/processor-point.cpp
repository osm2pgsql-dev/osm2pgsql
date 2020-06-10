#include <memory>

#include "processor-point.hpp"

processor_point::processor_point(std::shared_ptr<reprojection> const &proj)
: geometry_processor(proj->target_srs(), "POINT", interest_node)
{}

geometry_processor::wkb_t
processor_point::process_node(osmium::Location const &loc,
                              geom::osmium_builder_t *builder)
{
    return builder->get_wkb_node(loc);
}
