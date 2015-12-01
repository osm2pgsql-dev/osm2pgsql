#include "geometry-processor.hpp"
#include "processor-line.hpp"
#include "processor-point.hpp"
#include "processor-polygon.hpp"
#include "middle.hpp"
#include "options.hpp"
#include "reprojection.hpp"

#include <boost/format.hpp>
#include <boost/optional.hpp>
#include <stdexcept>
#include <memory>

std::shared_ptr<geometry_processor> geometry_processor::create(const std::string &type,
                                                                 const options_t *options) {
    std::shared_ptr<geometry_processor> ptr;
    int srid = options->projection->project_getprojinfo()->srs;

    if (type == "point") {
        ptr = std::make_shared<processor_point>(srid);
    }
    else if (type == "line") {
        ptr = std::make_shared<processor_line>(srid);
    }
    else if (type == "polygon") {
        ptr = std::make_shared<processor_polygon>(srid, options->enable_multi);
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

geometry_builder::pg_geom_t geometry_processor::process_node(double, double) {
    return geometry_builder::pg_geom_t();
}

geometry_builder::pg_geom_t geometry_processor::process_way(const nodelist_t &) {
    return geometry_builder::pg_geom_t();
}

geometry_builder::pg_geoms_t geometry_processor::process_relation(const multinodelist_t &) {
    return geometry_builder::pg_geoms_t();
}

way_helper::way_helper()
{
}
way_helper::~way_helper()
{
}
size_t way_helper::set(const idlist_t &node_ids, const middle_query_t *mid)
{
    node_cache.clear();
    mid->nodes_get_list(node_cache, node_ids);

    // equivalent to returning node_count for complete ways, different for partial extracts
    return node_cache.size();
}

relation_helper::relation_helper()
{
}

relation_helper::~relation_helper()
{
}

size_t relation_helper::set(const memberlist_t *member_list, const middle_t* mid)
{
    // cleanup
    input_way_ids.clear();
    ways.clear();
    tags.clear();
    nodes.clear();
    roles.clear();

    //keep a few things
    members = member_list;

    //grab the way members' ids
    input_way_ids.reserve(member_list->size());
    for (memberlist_t::const_iterator it = members->begin(); it != members->end(); ++it) {
        if(it->type == OSMTYPE_WAY)
            input_way_ids.push_back(it->id);
    }

    //if we didn't end up using any we'll bail
    if (input_way_ids.empty())
        return 0;

    //get the nodes of the ways
    mid->ways_get_list(input_way_ids, ways, tags, nodes);

    //grab the roles of each way
    roles.reserve(ways.size());
    size_t memberpos = 0;
    for (idlist_t::const_iterator it = ways.begin(); it != ways.end(); ++it) {
        while (memberpos < members->size()) {
            if (members->at(memberpos).id == *it) {
                roles.push_back(&(members->at(memberpos).role));
                memberpos++;
                break;
            }
            memberpos++;
        }
    }

    //mark the ends of each so whoever uses them will know where they end..
    superseeded.resize(ways.size());

    return ways.size();
}
