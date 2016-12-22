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
    int srid = options->projection->target_srs();

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
size_t way_helper::set(osmium::WayNodeList const &node_ids,
                       const middle_query_t *mid)
{
    node_cache.clear();
    mid->nodes_get_list(node_cache, node_ids);

    // equivalent to returning node_count for complete ways, different for partial extracts
    return node_cache.size();
}

relation_helper::relation_helper()
: data(1024, osmium::memory::Buffer::auto_grow::yes)
{}

relation_helper::~relation_helper() = default;

size_t relation_helper::set(osmium::RelationMemberList const &member_list, middle_t const *mid)
{
    // cleanup
    input_way_ids.clear();
    data.clear();
    roles.clear();

    //grab the way members' ids
    size_t num_input = member_list.size();
    input_way_ids.reserve(num_input);
    roles.reserve(num_input);
    for (auto const &member : member_list) {
        if (member.type() == osmium::item_type::way) {
            input_way_ids.push_back(member.ref());
            roles.push_back(member.role());
        }
    }

    //if we didn't end up using any we'll bail
    if (input_way_ids.empty())
        return 0;

    //get the nodes of the ways
    auto num_ways = mid->ways_get_list(input_way_ids, data);

    //grab the roles of each way
    if (num_ways < input_way_ids.size()) {
        size_t memberpos = 0;
        size_t waypos = 0;
        for (auto const &w : data.select<osmium::Way>()) {
            while (memberpos < input_way_ids.size()) {
                if (input_way_ids[memberpos] == w.id()) {
                    roles[waypos] = roles[memberpos];
                    ++memberpos;
                    break;
                }
                ++memberpos;
            }
            ++waypos;
        }
        roles.resize(num_ways);
    }

    //mark the ends of each so whoever uses them will know where they end..
    superseeded.resize(num_ways);

    return num_ways;
}

multitaglist_t relation_helper::get_filtered_tags(tagtransform *transform, export_list const &el) const
{
    multitaglist_t filtered(roles.size());

    size_t i = 0;
    for (auto const &w : data.select<osmium::Way>()) {
        transform->filter_tags(w, nullptr, nullptr, el, filtered[i++]);
    }

    return filtered;
}

multinodelist_t relation_helper::get_nodes(middle_t const *mid) const
{
    multinodelist_t nodes(roles.size());

    size_t i = 0;
    for (auto const &w : data.select<osmium::Way>()) {
        mid->nodes_get_list(nodes[i++], w.nodes());
    }

    return nodes;
}
