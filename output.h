/* Common output layer interface */

/* Each output layer must provide methods for 
 * storing:
 * - Nodes (Points of interest etc)
 * - Way geometries
 * Associated tags: name, type etc. 
*/

#ifndef OUTPUT_H
#define OUTPUT_H

#include "middle.h"
#include "keyvals.h"

struct output_options {
  const char *conninfo;  /* Connection info string */
  const char *prefix;    /* prefix for table names */
  int scale;       /* scale for converting coordinates to fixed point */
  int projection;  /* SRS of projection */
  int append;      /* Append to existing data */
  int slim;        /* In slim mode */
  int cache;       /* Memory usable for cache in MB */
  struct middle_t *mid;  /* Mid storage to use */
  const char *style;     /* style file to use */
  int expire_tiles_zoom;	/* Zoom level for tile expiry list */
  int expire_tiles_zoom_min;	/* Minimum zoom level for tile expiry list */
  const char *expire_tiles_filename;	/* File name to output expired tiles list to */
};

struct output_t {
    int (*start)(const struct output_options *options);
    void (*stop)();
    void (*cleanup)(void);
//    void (*process)(struct middle_t *mid);
//    int (*node)(int id, struct keyval *tags, double node_lat, double node_lon);
//    int (*way)(int id, struct keyval *tags, struct osmNode *nodes, int count);
//    int (*relation)(int id, struct keyval *rel_tags, struct osmNode **nodes, struct keyval **tags, int *count);

    int (*node_add)(int id, double lat, double lon, struct keyval *tags);
    int (*way_add)(int id, int *nodes, int node_count, struct keyval *tags);
    int (*relation_add)(int id, struct member *members, int member_count, struct keyval *tags);

    int (*node_modify)(int id, double lat, double lon, struct keyval *tags);
    int (*way_modify)(int id, int *nodes, int node_count, struct keyval *tags);
    int (*relation_modify)(int id, struct member *members, int member_count, struct keyval *tags);

    int (*node_delete)(int id);
    int (*way_delete)(int id);
    int (*relation_delete)(int id);
};

unsigned int pgsql_filter_tags(enum OsmType type, struct keyval *tags, int *polygon);

#endif
