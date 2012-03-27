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

/* Variants for generation of hstore column */
/* No hstore column */
#define HSTORE_NONE 0
/* create a hstore column for all tags which do not have an exclusive column */
#define HSTORE_NORM 1
/* create a hstore column for all tags */
#define HSTORE_ALL 2

struct output_options {
  const char *conninfo;  /* Connection info string */
  const char *prefix;    /* prefix for table names */
  int scale;       /* scale for converting coordinates to fixed point */
  int projection;  /* SRS of projection */
  int append;      /* Append to existing data */
  int slim;        /* In slim mode */
  int cache;       /* Memory usable for cache in MB */
  struct middle_t *mid;  /* Mid storage to use */
  struct output_t *out;  /* Output type used */
  const char *tblsmain_index;     /* Pg Tablespace to store indexes on main tables */
  const char *tblsslim_index;     /* Pg Tablespace to store indexes on slim tables */
  const char *tblsmain_data;     /* Pg Tablespace to store main tables */
  const char *tblsslim_data;     /* Pg Tablespace to store slim tables */
  const char *style;     /* style file to use */
  int expire_tiles_zoom;	/* Zoom level for tile expiry list */
  int expire_tiles_zoom_min;	/* Minimum zoom level for tile expiry list */
  const char *expire_tiles_filename;	/* File name to output expired tiles list to */
  int enable_hstore; /* add an additional hstore column with objects key/value pairs */
  int enable_multi; /* Output multi-geometries intead of several simple geometries */
  const char** hstore_columns; /* list of columns that should be written into their own hstore column */
  int n_hstore_columns; /* number of hstore columns */
  int keep_coastlines;
  int parallel_indexing;
  int alloc_chunkwise;
  int num_procs;
  int droptemp; /* drop slim mode temp tables after act */
  int unlogged; /* use unlogged tables where possible */
  int hstore_match_only; /* only copy rows that match an explicitly listed key */
};

struct output_t {
    int (*start)(const struct output_options *options);
    int (*connect)(const struct output_options *options, int startTransaction);
    void (*stop)();
    void (*cleanup)(void);
    void (*close)(int stopTransaction);
//    void (*process)(struct middle_t *mid);
//    int (*node)(osmid_t id, struct keyval *tags, double node_lat, double node_lon);
//    int (*way)(osmid_t id, struct keyval *tags, struct osmNode *nodes, int count);
//    int (*relation)(osmid_t id, struct keyval *rel_tags, struct osmNode **nodes, struct keyval **tags, int *count);

    int (*node_add)(osmid_t id, double lat, double lon, struct keyval *tags);
    int (*way_add)(osmid_t id, osmid_t *nodes, int node_count, struct keyval *tags);
    int (*relation_add)(osmid_t id, struct member *members, int member_count, struct keyval *tags);

    int (*node_modify)(osmid_t id, double lat, double lon, struct keyval *tags);
    int (*way_modify)(osmid_t id, osmid_t *nodes, int node_count, struct keyval *tags);
    int (*relation_modify)(osmid_t id, struct member *members, int member_count, struct keyval *tags);

    int (*node_delete)(osmid_t id);
    int (*way_delete)(osmid_t id);
    int (*relation_delete)(osmid_t id);
};

unsigned int pgsql_filter_tags(enum OsmType type, struct keyval *tags, int *polygon);

#endif
