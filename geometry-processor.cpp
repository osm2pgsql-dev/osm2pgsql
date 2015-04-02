#include "geometry-processor.hpp"
#include "processor-line.hpp"
#include "processor-point.hpp"
#include "processor-polygon.hpp"


#include <boost/make_shared.hpp>
#include <boost/format.hpp>
#include <stdexcept>

boost::shared_ptr<geometry_processor> geometry_processor::create(const std::string &type,
                                                                 const options_t *options) {
    boost::shared_ptr<geometry_processor> ptr;
    int srid = options->projection->project_getprojinfo()->srs;
    double scale = options->scale;

    if (type == "point") {
        ptr = boost::make_shared<processor_point>(srid, scale);
    }
    else if (type == "line") {
        ptr = boost::make_shared<processor_line>(srid);
    }
    else if (type == "polygon") {
        ptr = boost::make_shared<processor_polygon>(srid, options->enable_multi);
    }
    else {
        throw std::runtime_error((boost::format("Unable to construct geometry processor "
                                                "because type `%1%' is not known.")
                                  % type).str());
    }

    return ptr;
}

geometry_processor::geometry_processor(int srid, const std::string &type, unsigned int interests)
    : m_srid(srid), m_type(type), m_interests(interests) {
}

geometry_processor::~geometry_processor() {
}

int geometry_processor::srid() const {
    return m_srid;
}

const std::string &geometry_processor::column_type() const {
    return m_type;
}

unsigned int geometry_processor::interests() const {
    return m_interests;
}

bool geometry_processor::interests(unsigned int interested) const {
    return (interested & m_interests) == interested;
}

geometry_builder::maybe_wkt_t geometry_processor::process_node(double lat, double lon) {
    return geometry_builder::maybe_wkt_t();
}

geometry_builder::maybe_wkt_t geometry_processor::process_way(const osmNode *nodes, const size_t node_count) {
    return geometry_builder::maybe_wkt_t();
}

geometry_builder::maybe_wkts_t geometry_processor::process_relation(const osmNode * const * nodes, const int* node_counts) {
    return geometry_builder::maybe_wkts_t();
}

way_helper::way_helper()
{
}
way_helper::~way_helper()
{
}
size_t way_helper::set(const osmid_t *node_ids, size_t node_count, const middle_query_t *mid)
{
    // other parts of the code assume that the node cache is the size of the way
    // TODO: Fix this, and use std::vector everywhere
    node_cache.resize(node_count);
    // get the node data, and resize the node cache in case there were missing nodes
    node_cache.resize(mid->nodes_get_list(&node_cache.front(), node_ids, node_count));
    // equivalent to returning node_count for complete ways, different for partial extractsx
    return node_cache.size();
}

relation_helper::relation_helper():members(NULL), member_count(0), way_count(0)
{
}
relation_helper::~relation_helper()
{
    //clean up
    for(size_t i = 0; i < way_count; ++i)
    {
        tags[i].resetList();
        free(nodes[i]);
    }
}

size_t& relation_helper::set(const member* member_list, const int member_list_length, const middle_t* mid)
{
    //clean up
    for(size_t i = 0; i < way_count; ++i)
    {
        tags[i].resetList();
        free(nodes[i]);
    }

    //keep a few things
    members = member_list;
    member_count = member_list_length;

    //grab the way members' ids
    input_way_ids.resize(member_count);
    size_t used = 0;
    for(size_t i = 0; i < member_count; ++i)
        if(members[i].type == OSMTYPE_WAY)
            input_way_ids[used++] = members[i].id;

    //if we didnt end up using any well bail
    if(used == 0)
    {
        way_count = 0;
        return way_count;
    }

    //get the nodes of the ways
    tags.resize(used + 1);
    node_counts.resize(used + 1);
    nodes.resize(used + 1);
    ways.resize(used + 1);
    //this is mildly abusive treating vectors like arrays but the memory is contiguous so...
    way_count = mid->ways_get_list(&input_way_ids.front(), used, &ways.front(), &tags.front(), &nodes.front(), &node_counts.front());

    //grab the roles of each way
    roles.resize(way_count + 1);
    roles[way_count] = NULL;
    for (size_t i = 0; i < way_count; ++i)
    {
        size_t j = i;
        for (; j < member_count; ++j)
        {
            if (members[j].id == ways[i])
            {
                break;
            }
        }
        roles[i] = members[j].role;
    }

    //mark the ends of each so whoever uses them will know where they end..
    nodes[way_count] = NULL;
    node_counts[way_count] = 0;
    ways[way_count] = 0;
    superseeded.resize(way_count);
    return way_count;
}
