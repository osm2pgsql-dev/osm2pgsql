#include "osmtypes.hpp"
#include "output-null.hpp"

struct middle_query_t;
struct options_t;

void output_null_t::cleanup() {
}

int output_null_t::start() {
    return 0;
}

void output_null_t::stop() {
}

void output_null_t::commit() {
}

void output_null_t::enqueue_ways(pending_queue_t &job_queue, osmid_t id, size_t output_id, size_t& added) {
}

int output_null_t::pending_way(osmid_t id, int exists) {
    return 0;
}

void output_null_t::enqueue_relations(pending_queue_t &job_queue, osmid_t id, size_t output_id, size_t& added) {
}

int output_null_t::pending_relation(osmid_t id, int exists) {
    return 0;
}

int output_null_t::node_add(osmid_t, double, double, const taglist_t &) {
  return 0;
}

int output_null_t::way_add(osmid_t a, const idlist_t &, const taglist_t &) {
  return 0;
}

int output_null_t::relation_add(osmid_t a, const memberlist_t &, const taglist_t &) {
  return 0;
}

int output_null_t::node_delete(osmid_t i) {
  return 0;
}

int output_null_t::way_delete(osmid_t i) {
  return 0;
}

int output_null_t::relation_delete(osmid_t i) {
  return 0;
}

int output_null_t::node_modify(osmid_t, double, double, const taglist_t &) {
  return 0;
}

int output_null_t::way_modify(osmid_t, const idlist_t &, const taglist_t &) {
  return 0;
}

int output_null_t::relation_modify(osmid_t, const memberlist_t &, const taglist_t &) {
  return 0;
}

std::shared_ptr<output_t> output_null_t::clone(const middle_query_t* cloned_middle) const {
    output_null_t *clone = new output_null_t(*this);
    clone->m_mid = cloned_middle;
    return std::shared_ptr<output_t>(clone);
}

output_null_t::output_null_t(const middle_query_t* mid_, const options_t &options_): output_t(mid_, options_) {
}

output_null_t::output_null_t(const output_null_t& other): output_t(other.m_mid, other.m_options) {
}

output_null_t::~output_null_t() {
}
