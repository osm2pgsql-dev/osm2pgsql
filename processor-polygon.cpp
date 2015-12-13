#include "processor-polygon.hpp"

processor_polygon::processor_polygon(int srid, bool enable_multi) : geometry_processor(srid, "GEOMETRY", interest_way | interest_relation), enable_multi(enable_multi)
{
}

processor_polygon::~processor_polygon()
{
}

geometry_builder::pg_geom_t processor_polygon::process_way(const nodelist_t &nodes)
{
    return builder.get_wkb_simple(nodes, true);
}

geometry_builder::pg_geoms_t processor_polygon::process_relation(const multinodelist_t &nodes)
{
    return  builder.build_polygons(nodes, enable_multi, -1);
}
