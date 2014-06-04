#include "geometry-processor.hpp"
#include "processor-point.hpp"

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

    } else {
        throw std::runtime_error((boost::format("Unable to construct geometry processor "
                                                "because type `%1%' is not known.")
                                  % type).str());
    }

    return ptr;
}

geometry_processor::geometry_processor(int srid, const std::string &type,
                                       geometry_processor::interest interests) 
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

geometry_processor::interest geometry_processor::interests() const {
    return m_interests;
}

geometry_processor::maybe_wkt_t geometry_processor::process_node(double lat, double lon) {
    return boost::none;
}

geometry_processor::maybe_wkt_t geometry_processor::process_way(osmid_t *nodes, int node_count,
                                                                const middle_query_t *mid) {
    return boost::none;
}

geometry_processor::maybe_wkt_t geometry_processor::process_relation(struct member *members,
                                                                     int member_count,
                                                                     const middle_query_t *mid) {
    return boost::none;
}
