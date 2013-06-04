/* Implements the output-layer processing for osm2pgsql
 * storing the data in several PostgreSQL tables
 * with the final PostGIS geometries for each entity
*/
 
#ifndef OUTPUT_PGSQL_H
#define OUTPUT_PGSQL_H

#include "output.h"

#define FLAG_POLYGON 1    /* For polygon table */
#define FLAG_LINEAR  2    /* For lines table */
#define FLAG_NOCACHE 4    /* Optimisation: don't bother remembering this one */
#define FLAG_DELETE  8    /* These tags should be simply deleted on sight */
#define FLAG_PHSTORE 17   /* polygons without own column but listed in hstore this implies FLAG_POLYGON */

/* Table columns, representing key= tags */
struct taginfo {
    char *name;
    char *type;
    int flags;
    int count;
};

struct relation_info {
    osmid_t id;
    struct keyval * tags;
    int member_count;
    const char ** member_roles;
    struct keyval * member_tags;
    int * member_way_node_count;
    struct osmNode ** member_way_nodes;
    osmid_t * member_ids;
};

struct way_info {
    osmid_t id;
    struct keyval * tags;
    struct osmNode *nodes;
    int node_count;
    int exists;
};
extern struct output_t out_pgsql;

#endif
