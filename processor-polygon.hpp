#ifndef PROCESSOR_POLYGON_HPP
#define PROCESSOR_POLYGON_HPP

#include "geometry-processor.hpp"

class processor_polygon : public geometry_processor
{
public:
    processor_polygon(std::shared_ptr<reprojection> const &proj);

    wkb_t process_way(osmium::Way const &nodes,
                      geom::osmium_builder_t *builder) override;
    wkbs_t process_relation(osmium::Relation const &rel,
                            osmium::memory::Buffer const &ways,
                            geom::osmium_builder_t *builder) override;
};

#endif /* PROCESSOR_POLYGON_HPP */
