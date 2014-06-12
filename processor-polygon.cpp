#include "processor-polygon.hpp"

#include <boost/format.hpp>

processor_polygon::processor_polygon(int srid, bool enable_multi) : geometry_processor(srid, "GEOMETRY", interest_way | interest_relation), enable_multi(enable_multi)
{
}

processor_polygon::~processor_polygon()
{
}

geometry_builder::maybe_wkt_t processor_polygon::process_way(const osmid_t *node_ids, const size_t node_count, const middle_query_t *mid)
{
    //if we don't have enough space already get more
    if(node_cache.size() < node_count)
        node_cache.resize(node_count);
    //get the node data
    int cached_count = mid->nodes_get_list(&node_cache.front(), node_ids, node_count);
    //have the builder make the wkt
    geometry_builder::maybe_wkt_t wkt = builder.get_wkt_simple(&node_cache.front(), cached_count, false);
    //hand back the wkt
    return wkt;
}

geometry_builder::maybe_wkts_t processor_polygon::process_relation(const osmNode * const * nodes, const int* node_counts, const middle_query_t *mid)
{
    //the hard word was already done for us in getting at the node data for each way. at this point just make the geom
    geometry_builder::maybe_wkts_t wkts  = builder.build(nodes, node_counts, true, enable_multi, -1);
    return wkts;
}
