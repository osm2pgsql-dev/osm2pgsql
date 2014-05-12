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
  osmid_t id = 1234;
  double lat = 12.3456789;
  double lon = 98.7654321;
  struct keyval tags;
  struct osmNode node;
  int status = 0;

  initList(&tags);

  // set the node
  status = mid->nodes_set(id, lat, lon, &tags);
  if (status != 0) { std::cerr << "ERROR: Unable to set node.\n"; return 1; }

  // get it back
  int count = mid->nodes_get_list(&node, &id, 1);
  if (count != 1) { std::cerr << "ERROR: Unable to get node list.\n"; return 1; }

  // check that it's the same
  if (node.lon != lon) { 
    std::cerr << "ERROR: Node should have lon=" << lon << ", but got back "
              << node.lon << " from middle.\n";
    return 1;
  }
  if (node.lat != lat) { 
    std::cerr << "ERROR: Node should have lat=" << lat << ", but got back "
              << node.lat << " from middle.\n";
    return 1;
  }

  // clean up for next test
  if (mid->nodes_delete) {
    mid->nodes_delete(id);
  }

  resetList(&tags);

  return 0;
}

// urgh, static variables... need to get rid of these as soon as possible...
static std::list<osmid_t> pending_ways;

int way_callback(osmid_t id, struct keyval *tags, struct osmNode *nodes, int count, int exists)
{
  // TODO: figure out exists - seems to be a useless variable?
  pending_ways.push_back(id);
  return 0; // looks like this is ignored anyway?
}

int test_way_set(middle_t *mid)
{
  osmid_t way_id = 1;
  double lat = 12.3456789;
  double lon = 98.7654321;
  struct keyval tags[2]; /* <-- this is needed because the ways_get_list method calls
                          * initList() on the `count + 1`th tags element. */
  struct osmNode *node_ptr = NULL;
  osmid_t *way_ids_ptr = NULL;
  int node_count = 0;
  int status = 0;
  osmid_t nds[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
  const int nd_count = ((sizeof nds) / (sizeof nds[0]));
  initList(&tags[0]);

  // set the nodes
  for (int i = 0; i < nd_count; ++i) {
    status = mid->nodes_set(nds[i], lat, lon, &tags[0]);
    if (status != 0) { std::cerr << "ERROR: Unable to set node " << nds[i] << ".\n"; return 1; }
  }

  // set the way
  status = mid->ways_set(way_id, nds, nd_count, &tags[0], 0);
  if (status != 0) { std::cerr << "ERROR: Unable to set way.\n"; return 1; }

  // commit the setup data
  mid->commit();

  // get it back
  int way_count = mid->ways_get_list(&way_id, 1, &way_ids_ptr, &tags[0], &node_ptr, &node_count);
  if (way_count != 1) { std::cerr << "ERROR: Unable to get way list.\n"; return 1; }

  // check that it's the same
  if (node_count != nd_count) { 
    std::cerr << "ERROR: Way should have " << nd_count << " nodes, but got back "
              << node_count << " from middle.\n";
    return 1;
  }
  if (way_ids_ptr[0] != way_id) {
    std::cerr << "ERROR: Way should have id=" << way_id << ", but got back "
              << way_ids_ptr[0] << " from middle.\n";
    return 1;
  }
  for (int i = 0; i < nd_count; ++i) {
    if (node_ptr[i].lon != lon) { 
      std::cerr << "ERROR: Way node should have lon=" << lon << ", but got back "
                << node_ptr[i].lon << " from middle.\n";
      return 1;
    }
    if (node_ptr[i].lat != lat) { 
      std::cerr << "ERROR: Way node should have lat=" << lat << ", but got back "
                << node_ptr[i].lat << " from middle.\n";
      return 1;
    }
  }

  // first, try with no pending ways
  pending_ways.clear();
  mid->iterate_ways(&way_callback);
  if (!pending_ways.empty()) {
    std::cerr << "ERROR: Was expecting no pending ways, but got " << pending_ways.size()
              << " from middle.\n";
    return 1;
  }

  // to set a pending way, we need to first delete it, but deletions aren't
  // supported by all middles... so we just skip it in that case and cross
  // our fingers.
  if (mid->ways_delete != NULL) {
    mid->ways_delete(way_id);
  }

  // now, with a pending way set.
  status = mid->ways_set(way_id, nds, nd_count, &tags[0], 1);
  if (status != 0) { std::cerr << "ERROR: Unable to set way pending.\n"; return 1; }

  pending_ways.clear();
  mid->iterate_ways(&way_callback);
  if (pending_ways.size() != 1) {
    std::cerr << "ERROR: Was expecting a single pending way, but got " 
              << pending_ways.size() << " from middle.\n";
    return 1;
  }

  // some middles don't support changing the nodes - they
  // don't have diff update ability. here, we will just
  // skip the test for that.
  if (mid->node_changed != NULL) {
    // need to delete again to reset the pending flag to zero.
    if (mid->ways_delete != NULL) {
      mid->ways_delete(way_id);
    }
    
    // finally, try touching a node on a non-pending way. that should
    // make it become pending.
    status = mid->ways_set(way_id, nds, nd_count, &tags[0], 0);
    if (status != 0) { std::cerr << "ERROR: Unable to set way not pending.\n"; return 1; }
    status = mid->node_changed(nds[0]);
    if (status != 0) { std::cerr << "ERROR: Unable to reset node.\n"; return 1; }
    pending_ways.clear();
    mid->iterate_ways(&way_callback);
    if (pending_ways.size() != 1) {
      std::cerr << "ERROR: Was expecting a single pending way from node update, but got " 
                << pending_ways.size() << " from middle.\n";
      return 1;
    }
  }
  
  resetList(&tags[0]);
  free(node_ptr);
  free(way_ids_ptr);

  // clean up for next test
  if ((mid->nodes_delete != NULL) && (mid->ways_delete != NULL)) {
    for (int i = 0; i < nd_count; ++i) {
      mid->nodes_delete(nds[i]);
    }
    mid->ways_delete(way_id);
  }

  // commit the torn-down data
  mid->commit();

  return 0;
}
