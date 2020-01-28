#include <stdexcept>

#include "reprojection.hpp"

std::shared_ptr<reprojection> reprojection::make_generic_projection(int srs)
{
    throw std::runtime_error("No generic projection library available.");
}
