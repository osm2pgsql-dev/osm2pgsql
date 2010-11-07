/* Data types to hold OSM node, segment, way data */

#ifndef OSMTYPES_H
#define OSMTYPES_H

#include "keyvals.h"

enum OsmType { OSMTYPE_WAY, OSMTYPE_NODE, OSMTYPE_RELATION };

struct osmNode {
    double lon;
    double lat;
};

struct member {
    enum OsmType type;
    int id;
    char *role;
};

typedef enum { FILETYPE_NONE, FILETYPE_OSM, FILETYPE_OSMCHANGE, FILETYPE_PLANETDIFF } filetypes_t;
typedef enum { ACTION_NONE, ACTION_CREATE, ACTION_MODIFY, ACTION_DELETE } actions_t;

struct osmdata_t {
  int count_node,    max_node;
  int count_way,     max_way;
  int count_rel,     max_rel;

  struct output_t *out;

/* Since {node,way} elements are not nested we can guarantee the 
   values in an end tag must match those of the corresponding 
   start tag and can therefore be cached.
*/
  double node_lon, node_lat;
  struct keyval tags;
  int *nds;
  int nd_count, nd_max;
  struct member *members;
  unsigned member_count, member_max;
  int osm_id;
  filetypes_t filetype;
  actions_t action;
  int extra_attributes;

  // Bounding box to filter imported data
  const char *bbox;

  double minlon, minlat, maxlon, maxlat;
};

void realloc_nodes(struct osmdata_t *osmdata);
void realloc_members(struct osmdata_t *osmdata);
void resetMembers(struct osmdata_t *osmdata);
void printStatus(struct osmdata_t *osmdata);
int node_wanted(struct osmdata_t *osmdata, double lat, double lon);

/* exit_nicely - called to cleanup after fatal error */
void exit_nicely(void);

#endif
