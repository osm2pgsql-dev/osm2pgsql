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

geometry_builder::maybe_wkt_t geometry_processor::process_way(const osmid_t *node_ids, const size_t node_count, const middle_query_t *mid) {
    return geometry_builder::maybe_wkt_t();
}

geometry_builder::maybe_wkt_t geometry_processor::process_way(const osmNode *nodes, const size_t node_count) {
    return geometry_builder::maybe_wkt_t();
}

geometry_builder::maybe_wkts_t geometry_processor::process_relation(const osmNode * const * nodes, const int* node_counts) {
    return geometry_builder::maybe_wkts_t();
}
