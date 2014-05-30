#include "geometry-processor.hpp"

boost::shared_ptr<geometry_processor> geometry_processor::create(const std::string &type) {
    // TODO: implement me!
    abort();
    return boost::shared_ptr<geometry_processor>();
}

geometry_processor::~geometry_processor() {
}

geometry_processor::interest geometry_processor::interests() const {
    return interest_NONE;
}
