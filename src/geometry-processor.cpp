#include "format.hpp"
#include "geometry-processor.hpp"
#include "middle.hpp"
#include "options.hpp"
#include "processor-line.hpp"
#include "processor-point.hpp"
#include "processor-polygon.hpp"
#include "reprojection.hpp"

#include <memory>
#include <stdexcept>

std::shared_ptr<geometry_processor>
geometry_processor::create(std::string const &type, options_t const *options)
{
    std::shared_ptr<geometry_processor> ptr;

    if (type == "point") {
        ptr = std::make_shared<processor_point>(options->projection);
    } else if (type == "line") {
        ptr = std::make_shared<processor_line>(options->projection);
    } else if (type == "polygon") {
        ptr = std::make_shared<processor_polygon>(options->projection,
                                                  options->enable_multi);
    } else {
        throw std::runtime_error{
            "Unable to construct geometry processor "
            "because type `{}' is not known."_format(type)};
    }

    return ptr;
}

geometry_processor::geometry_processor(int srid, std::string const &type,
                                       unsigned int interests)
: m_srid(srid), m_type(type), m_interests(interests)
{}

geometry_processor::~geometry_processor() = default;

int geometry_processor::srid() const noexcept { return m_srid; }

std::string const &geometry_processor::column_type() const noexcept
{
    return m_type;
}

unsigned int geometry_processor::interests() const noexcept
{
    return m_interests;
}

bool geometry_processor::interests(unsigned int interested) const noexcept
{
    return (interested & m_interests) == interested;
}

geometry_processor::wkb_t
geometry_processor::process_node(osmium::Location const &,
                                 geom::osmium_builder_t *)
{
    return wkb_t{};
}

geometry_processor::wkb_t
geometry_processor::process_way(osmium::Way const &, geom::osmium_builder_t *)
{
    return wkb_t{};
}

geometry_processor::wkbs_t
geometry_processor::process_relation(osmium::Relation const &,
                                     osmium::memory::Buffer const &,
                                     geom::osmium_builder_t *)
{
    return wkbs_t{};
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
    return mid->rel_way_members_get(rel, &roles, data);
}

void relation_helper::add_way_locations(middle_query_t const *mid)
{
    for (auto &w : data.select<osmium::Way>()) {
        mid->nodes_get_list(&(w.nodes()));
    }
}
