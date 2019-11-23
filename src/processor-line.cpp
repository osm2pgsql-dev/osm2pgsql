#include "processor-line.hpp"

processor_line::processor_line(std::shared_ptr<reprojection> const &proj)
: geometry_processor(proj->target_srs(), "LINESTRING",
                     interest_way | interest_relation)
{}

geometry_processor::wkb_t
processor_line::process_way(osmium::Way const &way,
                            geom::osmium_builder_t *builder)
{
    auto wkbs = builder->get_wkb_line(way.nodes(), 1000000);

    return wkbs.empty() ? wkb_t() : wkbs[0];
}

geometry_processor::wkbs_t
processor_line::process_relation(osmium::Relation const &,
                                 osmium::memory::Buffer const &ways,
                                 geom::osmium_builder_t *builder)
{
    return builder->get_wkb_multiline(ways, 1000000);
}
