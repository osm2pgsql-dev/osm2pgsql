#include <iostream>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <cassert>
#include <list>

#include "osmtypes.hpp"
#include "tests/middle-tests.hpp"

int test_node_set(middle_t *mid)
{
  idlist_t ids;
  osmid_t id = 1234;
  double lat = 12.3456789;
  double lon = 98.7654321;
  taglist_t tags;
  nodelist_t nodes;
  int status = 0;

  // set the node
  status = mid->nodes_set(id, lat, lon, tags);
  if (status != 0) { std::cerr << "ERROR: Unable to set node.\n"; return 1; }

  // get it back
  ids.push_back(id);
  int count = mid->nodes_get_list(nodes, ids);
  if (count != 1) { std::cerr << "ERROR: Unable to get node list.\n"; return 1; }

  // check that it's the same
  if (nodes[0].lon != lon) {
    std::cerr << "ERROR: Node should have lon=" << lon << ", but got back "
              << nodes[0].lon << " from middle.\n";
    return 1;
  }
  if (nodes[0].lat != lat) {
    std::cerr << "ERROR: Node should have lat=" << lat << ", but got back "
              << nodes[0].lat << " from middle.\n";
    return 1;
  }

  // clean up for next test
  if (dynamic_cast<slim_middle_t *>(mid)) {
    dynamic_cast<slim_middle_t *>(mid)->nodes_delete(id);
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
  struct osmNode *node_ptr = NULL;
  int status = 0;
  idlist_t nds;
  for (osmid_t i = 1; i <= 10; ++i)
      nds.push_back(i);

  // set the nodes
  for (int i = 0; i < nds.size(); ++i) {
    status = mid->nodes_set(nds[i], lat, lon, tags);
    if (status != 0) { std::cerr << "ERROR: Unable to set node " << nds[i] << ".\n"; return 1; }
  }

  // set the way
  status = mid->ways_set(way_id, nds, tags);
  if (status != 0) { std::cerr << "ERROR: Unable to set way.\n"; return 1; }

  // commit the setup data
  mid->commit();

  // get it back
  idlist_t ways, xways;
  ways.push_back(way_id);
  std::vector<taglist_t> xtags;
  multinodelist_t xnodes;
  int way_count = mid->ways_get_list(ways, xways, xtags, xnodes);
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
  for (int i = 0; i < nds.size(); ++i) {
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
      status = slim->node_changed(nds[0]);
      if (status != 0) { std::cerr << "ERROR: Unable to reset node.\n"; return 1; }
      slim->iterate_ways(tpp);
      if (slim->pending_count() != 1) {
          std::cerr << "ERROR: Was expecting a single pending way from node update, but got "
                    << slim->pending_count() << " from middle.\n";
          return 1;
      }
  }

  // clean up for next test
  if (dynamic_cast<slim_middle_t *>(mid)) {
      slim_middle_t *slim = dynamic_cast<slim_middle_t *>(mid);

      for (int i = 0; i < nds.size(); ++i) {
          slim->nodes_delete(nds[i]);
      }
      slim->ways_delete(way_id);
  }

  // commit the torn-down data
  mid->commit();

  return 0;
}
