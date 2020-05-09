#include "output-null.hpp"
#include "osmtypes.hpp"

std::shared_ptr<output_t>
output_null_t::clone(std::shared_ptr<middle_query_t> const &mid,
                     std::shared_ptr<db_copy_thread_t> const &) const
{
    return std::make_shared<output_null_t>(mid, m_options);
}

output_null_t::output_null_t(std::shared_ptr<middle_query_t> const &mid,
                             options_t const &options)
: output_t(mid, options)
{}

output_null_t::~output_null_t() = default;
