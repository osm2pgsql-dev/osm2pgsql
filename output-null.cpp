#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

#include "osmtypes.hpp"
#include "output.hpp"

#define UNUSED  __attribute__ ((unused))

static void null_out_cleanup(void) {
}

static int null_out_start(const struct output_options *opt UNUSED) {
    return 0;
}

static void null_out_stop() {
}

static int null_add_node(osmid_t a UNUSED, double b UNUSED, double c UNUSED, struct keyval *k UNUSED) {
  return 0;
}

static int null_add_way(osmid_t a UNUSED, osmid_t *b UNUSED, int c UNUSED, struct keyval *k UNUSED) {
  return 0;
}

static int null_add_relation(osmid_t a UNUSED, struct member *b UNUSED, int c UNUSED, struct keyval *k UNUSED) {
  return 0;
}

static int null_delete_node(osmid_t i UNUSED) {
  return 0;
}

static int null_delete_way(osmid_t i UNUSED) {
  return 0;
}

static int null_delete_relation(osmid_t i UNUSED) {
  return 0;
}

static int null_modify_node(osmid_t a UNUSED, double b UNUSED, double c UNUSED, struct keyval * k UNUSED) {
  return 0;
}

static int null_modify_way(osmid_t a UNUSED, osmid_t * b UNUSED, int c UNUSED, struct keyval * k UNUSED) {
  return 0;
}

static int null_modify_relation(osmid_t a UNUSED, struct member * b UNUSED, int c UNUSED, struct keyval * k UNUSED) {
  return 0;
}

struct output_t make_null() {
    output_t nul;
    memset(&nul, 0, sizeof nul);

    nul.start           = null_out_start;
    nul.stop            = null_out_stop;
    nul.cleanup         = null_out_cleanup;
    nul.node_add        = null_add_node;
    nul.way_add         = null_add_way;
    nul.relation_add    = null_add_relation;
    
    nul.node_modify     = null_modify_node;
    nul.way_modify      = null_modify_way;
    nul.relation_modify = null_modify_relation;
    
    nul.node_delete     = null_delete_node;
    nul.way_delete      = null_delete_way;
    nul.relation_delete = null_delete_relation;

    return nul;
}

struct output_t out_null = make_null();
