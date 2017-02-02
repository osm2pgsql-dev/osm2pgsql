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

    if (type == "point") {
        ptr = std::make_shared<processor_point>(options->projection);
    }
    else if (type == "line") {
        ptr = std::make_shared<processor_line>(options->projection);
    }
    else if (type == "polygon") {
        ptr = std::make_shared<processor_polygon>(options->projection,
                                                  options->enable_multi);
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

geometry_processor::~geometry_processor() = default;

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

geometry_processor::wkb_t
geometry_processor::process_node(osmium::Location const &)
{
    return wkb_t();
}

geometry_processor::wkb_t geometry_processor::process_way(osmium::Way const &)
{
    return wkb_t();
}

geometry_processor::wkbs_t
geometry_processor::process_relation(osmium::Relation const &,
                                     osmium::memory::Buffer const &)
{
    return wkbs_t();
}


relation_helper::relation_helper()
: data(1024, osmium::memory::Buffer::auto_grow::yes)
{}


size_t relation_helper::set(osmium::RelationMemberList const &member_list, middle_t const *mid)
{
    // cleanup
    input_way_ids.clear();
    data.clear();
    roles.clear();

    //grab the way members' ids
    for (auto const &m : member_list) {
        /* Need to handle more than just ways... */
        if (m.type() == osmium::item_type::way) {
            input_way_ids.push_back(m.ref());
        }
    }

    //if we didn't end up using any we'll bail
    if (input_way_ids.empty())
        return 0;

    //get the nodes of the ways
    auto num_ways = mid->ways_get_list(input_way_ids, data);

    //grab the roles of each way
    for (auto const &w : data.select<osmium::Way>()) {
        for (auto const &member : member_list) {
            if (member.ref() == w.id() &&
                member.type() == osmium::item_type::way) {
                roles.emplace_back(member.role());
                break;
            }
        }
    }

    //mark the ends of each so whoever uses them will know where they end..
    superseded.resize(num_ways);

    return num_ways;
}

multitaglist_t relation_helper::get_filtered_tags(tagtransform *transform, export_list const &el) const
{
    multitaglist_t filtered(roles.size());

    size_t i = 0;
    for (auto const &w : data.select<osmium::Way>()) {
        transform->filter_tags(w, nullptr, nullptr, el, filtered[++i]);
    }

    return filtered;
}

void relation_helper::add_way_locations(middle_t const *mid)
{
    for (auto &w : data.select<osmium::Way>()) {
        mid->nodes_get_list(&(w.nodes()));
    }
}
