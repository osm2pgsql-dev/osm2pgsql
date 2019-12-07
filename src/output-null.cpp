#include "output-null.hpp"
#include "osmtypes.hpp"

void output_null_t::cleanup() {}

int output_null_t::start() { return 0; }

void output_null_t::stop(osmium::thread::Pool *) {}

void output_null_t::commit() {}

void output_null_t::enqueue_ways(pending_queue_t &, osmid_t, size_t, size_t &)
{}

int output_null_t::pending_way(osmid_t, int) { return 0; }

void output_null_t::enqueue_relations(pending_queue_t &, osmid_t, size_t,
                                      size_t &)
{}

int output_null_t::pending_relation(osmid_t, int) { return 0; }

std::shared_ptr<output_t>
output_null_t::clone(std::shared_ptr<middle_query_t> const &mid,
                     std::shared_ptr<db_copy_thread_t> const &) const
{
    return std::shared_ptr<output_t>(new output_null_t(mid, m_options));
}

output_null_t::output_null_t(std::shared_ptr<middle_query_t> const &mid,
                             options_t const &options)
: output_t(mid, options)
{}

output_null_t::~output_null_t() = default;
