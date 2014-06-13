#include "processor-line.hpp"

#include <boost/format.hpp>

processor_line::processor_line(int srid) : geometry_processor(srid, "LINESTRING", interest_way)
{
}

processor_line::~processor_line()
{
}

geometry_builder::maybe_wkt_t processor_line::process_way(const osmid_t *node_ids, size_t node_count, const middle_query_t *mid)
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

geometry_builder::maybe_wkt_t processor_line::process_way(const osmNode *nodes, size_t node_count)
{
    //have the builder make the wkt
    geometry_builder::maybe_wkt_t wkt = builder.get_wkt_simple(nodes, node_count, false);
    //hand back the wkt
    return wkt;
}
