#include <iostream>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <cassert>
#include <list>
#include <tuple>

#include "osmtypes.hpp"
#include "tests/middle-tests.hpp"

#define BLOCK_SHIFT 13
#define PER_BLOCK  (((osmid_t)1) << BLOCK_SHIFT)

struct expected_node {
  osmid_t id;
  double lon;
  double lat;

  expected_node() : id(0), lon(NAN), lat(NAN) {}

  expected_node(osmid_t id, double x, double y) : id(id), lon(x), lat(y) {}
};

typedef std::vector<expected_node> expected_nodelist_t;

#define ALLOWED_ERROR 10e-9
bool node_okay(osmNode node, expected_node expected) {
  if ((node.lat > expected.lat + ALLOWED_ERROR) || (node.lat < expected.lat - ALLOWED_ERROR)) {
    std::cerr << "ERROR: Node should have lat=" << expected.lat << ", but got back "
              << node.lat << " from middle.\n";
    return false;
  }
  if ((node.lon > expected.lon + ALLOWED_ERROR) || (node.lon < expected.lon - ALLOWED_ERROR)) {
    std::cerr << "ERROR: Node should have lon=" << expected.lon << ", but got back "
              << node.lon << " from middle.\n";
    return false;
  }
  return true;
}

int test_node_set(middle_t *mid)
{
  idlist_t ids;
  expected_node expected(1234, 12.3456789, 98.7654321);
  taglist_t tags;
  nodelist_t nodes;

  // set the node
  mid->nodes_set(expected.id, expected.lat, expected.lon, tags);

  // get it back
  ids.push_back(expected.id);
  if (mid->nodes_get_list(nodes, ids) != ids.size()) { std::cerr << "ERROR: Unable to get node list.\n"; return 1; }
  if (nodes.size() != ids.size()) { std::cerr << "ERROR: Mismatch in returned node list size.\n"; return 1; }

  // check that it's the same
  if (!node_okay(nodes[0], expected)) {
    return 1;
  }

  return 0;
}

inline double test_lat(osmid_t id) {
  return 1 + 1e-5 * id;
}

int test_nodes_comprehensive_set(middle_t *mid)
{
  taglist_t tags;

  expected_nodelist_t expected_nodes;
  expected_nodes.reserve(PER_BLOCK*8+1);

  // 2 dense blocks, the second partially filled at the star
  for (osmid_t id = 0; id < (PER_BLOCK+(PER_BLOCK >> 1) + 1); ++id)
  {
    expected_nodes.emplace_back(id, test_lat(id), 0.0);
  }

  // 1 dense block, 75% filled
  for (osmid_t id = PER_BLOCK*2; id < PER_BLOCK*3; ++id)
  {
    if ((id % 4 == 0) || (id % 4 == 1) || (id % 4 == 2))
      expected_nodes.emplace_back(id, test_lat(id), 0.0);
  }

  // 1 dense block, sparsly filled
  for (osmid_t id = PER_BLOCK*3; id < PER_BLOCK*4; ++id)
  {
    if (id % 4 == 0)
      expected_nodes.emplace_back(id, test_lat(id), 0.0);
  }

  // A lone sparse node
  expected_nodes.emplace_back(PER_BLOCK*5, test_lat(PER_BLOCK*5), 0.0);

  // A dense block of alternating positions of zero/non-zero
  for (osmid_t id = PER_BLOCK*6; id < PER_BLOCK*7; ++id)
  {
    if (id % 2 == 0)
      expected_nodes.emplace_back(id, 0.0, 0.0);
    else
      expected_nodes.emplace_back(id, test_lat(id), 0.0);
  }
  expected_nodes.emplace_back(PER_BLOCK*8, 0.0, 0.0);
  expected_nodes.emplace_back(PER_BLOCK*8+1, 0.0, 0.0);

  // Load up the nodes into the middle
  idlist_t ids;
  ids.reserve(expected_nodes.size());

  for (expected_nodelist_t::iterator node = expected_nodes.begin(); node != expected_nodes.end(); ++node)
  {
    mid->nodes_set(node->id, node->lat, node->lon, tags);
    ids.push_back(node->id);
  }

  nodelist_t nodes;
  if (mid->nodes_get_list(nodes, ids) != ids.size()) { std::cerr << "ERROR: Unable to get node list.\n"; return 1; }

  if (nodes.size() != ids.size()) { std::cerr << "ERROR: Mismatch in returned node list size.\n"; return 1; }

  for (size_t i = 0; i < nodes.size(); ++i)
  {
    if (!node_okay(nodes[i], expected_nodes[i])) {
      return 1;
    }
  }
  return 0;
}

struct test_pending_processor : public middle_t::pending_processor {
    test_pending_processor(): pending_ways(), pending_rels() {}
    virtual ~test_pending_processor() {}
    virtual void enqueue_ways(osmid_t id) {
        pending_ways.push_back(id);
    }
    virtual void process_ways() {
        pending_ways.clear();
    }
    virtual void enqueue_relations(osmid_t id) {
        pending_rels.push_back(id);
    }
    virtual void process_relations() {
        pending_rels.clear();
    }
    virtual int thread_count() {
        return 0;
    }
    virtual int size() {
        return pending_ways.size() + pending_rels.size();
    }
    std::list<osmid_t> pending_ways;
    std::list<osmid_t> pending_rels;
};

int test_way_set(middle_t *mid)
{
  osmid_t way_id = 1;
  double lat = 12.3456789;
  double lon = 98.7654321;
  taglist_t tags;
  struct osmNode *node_ptr = nullptr;
  idlist_t nds;
  for (osmid_t i = 1; i <= 10; ++i)
      nds.push_back(i);

  // set the nodes
  for (size_t i = 0; i < nds.size(); ++i) {
    mid->nodes_set(nds[i], lat, lon, tags);
  }

  // set the way
  mid->ways_set(way_id, nds, tags);

  // commit the setup data
  mid->commit();

  // get it back
  idlist_t ways, xways;
  ways.push_back(way_id);
  std::vector<taglist_t> xtags;
  multinodelist_t xnodes;
  size_t way_count = mid->ways_get_list(ways, xways, xtags, xnodes);
  if (way_count != 1) { std::cerr << "ERROR: Unable to get way list.\n"; return 1; }

  // check that it's the same
  if (xnodes[0].size() != nds.size()) {
    std::cerr << "ERROR: Way should have " << nds.size() << " nodes, but got back "
              << xnodes[0].size() << " from middle.\n";
    return 1;
  }
  if (xways[0] != way_id) {
    std::cerr << "ERROR: Way should have id=" << way_id << ", but got back "
              << xways[0] << " from middle.\n";
    return 1;
  }
  for (size_t i = 0; i < nds.size(); ++i) {
    if (xnodes[0][i].lon != lon) {
      std::cerr << "ERROR: Way node should have lon=" << lon << ", but got back "
                << node_ptr[i].lon << " from middle.\n";
      return 1;
    }
    if (xnodes[0][i].lat != lat) {
      std::cerr << "ERROR: Way node should have lat=" << lat << ", but got back "
                << node_ptr[i].lat << " from middle.\n";
      return 1;
    }
  }

  // the way we just inserted should not be pending
  test_pending_processor tpp;
  mid->iterate_ways(tpp);
  if (mid->pending_count() != 0) {
    std::cerr << "ERROR: Was expecting no pending ways, but got "
              << mid->pending_count() << " from middle.\n";
    return 1;
  }

  // some middles don't support changing the nodes - they
  // don't have diff update ability. here, we will just
  // skip the test for that.
  if (dynamic_cast<slim_middle_t *>(mid)) {
      slim_middle_t *slim = dynamic_cast<slim_middle_t *>(mid);

      // finally, try touching a node on a non-pending way. that should
      // make it become pending. we just checked that the way is not
      // pending, so any change must be due to the node changing.
      slim->node_changed(nds[0]);
      slim->iterate_ways(tpp);
      if (slim->pending_count() != 1) {
          std::cerr << "ERROR: Was expecting a single pending way from node update, but got "
                    << slim->pending_count() << " from middle.\n";
          return 1;
      }
  }

  return 0;
}
