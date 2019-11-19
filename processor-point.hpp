#ifndef OSM2PGSQL_PROCESSOR_POINT_HPP
#define OSM2PGSQL_PROCESSOR_POINT_HPP

#include "geometry-processor.hpp"

class processor_point : public geometry_processor
{
public:
    processor_point(std::shared_ptr<reprojection> const &proj);

    wkb_t process_node(osmium::Location const &loc,
                       geom::osmium_builder_t *builder) override;
};

#endif // OSM2PGSQL_PROCESSOR_POINT_HPP
