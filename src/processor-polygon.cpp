#include "processor-polygon.hpp"

processor_polygon::processor_polygon(std::shared_ptr<reprojection> const &proj,
                                     bool build_multigeoms)
: geometry_processor(proj->target_srs(), "GEOMETRY",
                     interest_way | interest_relation),
  m_build_multigeoms(build_multigeoms)
{}

geometry_processor::wkb_t
processor_polygon::process_way(osmium::Way const &way,
                               geom::osmium_builder_t *builder)
{
    return builder->get_wkb_polygon(way);
}

geometry_processor::wkbs_t
processor_polygon::process_relation(osmium::Relation const &rel,
                                    osmium::memory::Buffer const &ways,
                                    geom::osmium_builder_t *builder)
{
    return builder->get_wkb_multipolygon(rel, ways, m_build_multigeoms);
}
