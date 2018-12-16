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
        ptr = std::make_shared<processor_polygon>(options->projection);
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
geometry_processor::process_node(osmium::Location const &,
                                 geom::osmium_builder_t *)
{
    return wkb_t();
}

geometry_processor::wkb_t
geometry_processor::process_way(osmium::Way const &, geom::osmium_builder_t *)
{
    return wkb_t();
}

geometry_processor::wkbs_t
geometry_processor::process_relation(osmium::Relation const &,
                                     osmium::memory::Buffer const &,
                                     geom::osmium_builder_t *)
{
    return wkbs_t();
}


relation_helper::relation_helper()
: data(1024, osmium::memory::Buffer::auto_grow::yes)
{}

size_t relation_helper::set(osmium::Relation const &rel,
                            middle_query_t const *mid)
{
    // cleanup
    data.clear();
    roles.clear();

    // get the nodes and roles of the ways
    auto num_ways = mid->rel_way_members_get(rel, &roles, data);

    return num_ways;
}

void relation_helper::add_way_locations(middle_query_t const *mid)
{
    for (auto &w : data.select<osmium::Way>()) {
        mid->nodes_get_list(&(w.nodes()));
    }
}
