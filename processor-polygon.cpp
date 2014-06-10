#include "processor-polygon.hpp"

#include <boost/format.hpp>

processor_polygon::processor_polygon(int srid) : geometry_processor(srid, "GEOMETRY", (interest_way | interest_relation))
{
}

processor_polygon::~processor_polygon()
{
}

geometry_builder::maybe_wkt_t processor_polygon::process_way(osmid_t *node_ids, size_t node_count, const middle_query_t *mid)
{
    geometry_builder::maybe_wkt_t wkt;

    return wkt;
}

geometry_builder::maybe_wkts_t processor_polygon::process_relation(member *members, size_t member_count, const middle_query_t *mid)
{
    geometry_builder::maybe_wkts_t wkts;

    return wkts;
}
