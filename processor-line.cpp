#include "processor-line.hpp"

#include <boost/format.hpp>

processor_line::processor_line(int srid) : geometry_processor(srid, "LINESTRING", interest_way /*| interest_relation*/)
{
}

processor_line::~processor_line()
{
}

geometry_builder::maybe_wkt_t processor_line::process_way(osmid_t *node_ids, size_t node_count, const middle_query_t *mid)
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

geometry_builder::maybe_wkts_t processor_line::process_relation(member *members, size_t member_count, const middle_query_t *mid)
{
    //TODO: this code is never actually used due to only being interested in interest_way in the
    //constructor we may at some point want to care about relations but we'll save that for later

    //grab the way members' ids
    size_t used = 0;
    osmid_t* way_ids = new osmid_t[member_count + 1];
    for(size_t i = 0; i < member_count; ++i)
    {
        if(members[i].type != OSMTYPE_WAY)
            continue;
        way_ids[used] = members[i].id;
        used++;
    }

    //get the nodes of the ways
    keyval* returned_tags = new keyval[used];
    int* returned_node_counts = new int[used];
    osmNode** returned_nodes = new osmNode*[used];
    osmid_t* returned_ways;
    size_t returned_way_count = mid->ways_get_list(way_ids, used, &returned_ways, returned_tags, returned_nodes, returned_node_counts);
    delete [] way_ids;

    //grab the roles of each way
    const char** roles = new const char*[returned_way_count + 1];
    roles[returned_way_count] = NULL;
    for (size_t i = 0; i < returned_way_count; ++i)
    {
        size_t j = i;
        for (; j < member_count; ++j)
        {
            if (members[j].id == returned_ways[i])
            {
                break;
            }
        }
        roles[i] = members[j].role;
    }
    returned_nodes[returned_way_count] = NULL;
    returned_node_counts[returned_way_count] = 0;
    returned_ways[returned_way_count] = 0;

    //actually get the wkt for each member, dont split the geom
    geometry_builder::maybe_wkts_t wkts = builder.build(returned_nodes, returned_node_counts, false, false, std::numeric_limits<double>::max());

    //do a bunch of cleanup
    for(size_t i = 0; i < returned_way_count; ++i)
    {
        resetList(&(returned_tags[i]));
        free(returned_nodes[i]);
    }
    delete [] returned_tags;
    delete [] returned_node_counts;
    delete [] returned_nodes;
    delete [] returned_ways;
    delete [] roles;

    //give back the list
    return wkts;
}
