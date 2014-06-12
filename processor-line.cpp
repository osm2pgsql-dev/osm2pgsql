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
    //allocate some space for the node data
    osmNode* returned_nodes = new osmNode[node_count];
    //get the node data
    int returned_node_count = mid->nodes_get_list(returned_nodes, node_ids, node_count);
    //have the builder make the wkt
    geometry_builder::maybe_wkt_t wkt = builder.get_wkt_simple(returned_nodes, returned_node_count, false);
    //clean up
    delete [] returned_nodes;
    //hand back the wkt
    return wkt;
}
