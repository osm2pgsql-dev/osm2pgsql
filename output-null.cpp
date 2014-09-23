#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

#include <boost/make_shared.hpp>

#include "osmtypes.hpp"
#include "output-null.hpp"
#include "options.hpp"

#define UNUSED  __attribute__ ((unused))

void output_null_t::cleanup() {
}

int output_null_t::start() {
    return 0;
}

middle_t::cb_func *output_null_t::way_callback() {
    return NULL;
}

middle_t::cb_func *output_null_t::relation_callback() {
    return NULL;
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

int output_null_t::node_add(osmid_t a UNUSED, double b UNUSED, double c UNUSED, struct keyval *k UNUSED) {
  return 0;
}

int output_null_t::way_add(osmid_t a UNUSED, osmid_t *b UNUSED, int c UNUSED, struct keyval *k UNUSED) {
  return 0;
}

int output_null_t::relation_add(osmid_t a UNUSED, struct member *b UNUSED, int c UNUSED, struct keyval *k UNUSED) {
  return 0;
}

int output_null_t::node_delete(osmid_t i UNUSED) {
  return 0;
}

int output_null_t::way_delete(osmid_t i UNUSED) {
  return 0;
}

int output_null_t::relation_delete(osmid_t i UNUSED) {
  return 0;
}

int output_null_t::node_modify(osmid_t a UNUSED, double b UNUSED, double c UNUSED, struct keyval * k UNUSED) {
  return 0;
}

int output_null_t::way_modify(osmid_t a UNUSED, osmid_t * b UNUSED, int c UNUSED, struct keyval * k UNUSED) {
  return 0;
}

int output_null_t::relation_modify(osmid_t a UNUSED, struct member * b UNUSED, int c UNUSED, struct keyval * k UNUSED) {
  return 0;
}

boost::shared_ptr<output_t> output_null_t::clone(const middle_query_t* cloned_middle) const {
    output_null_t *clone = new output_null_t(*this);
    clone->m_mid = cloned_middle;
    return boost::shared_ptr<output_t>(clone);
}

output_null_t::output_null_t(const middle_query_t* mid_, const options_t &options_): output_t(mid_, options_) {
}

output_null_t::output_null_t(const output_null_t& other): output_t(other.m_mid, other.m_options) {
}

output_null_t::~output_null_t() {
}
