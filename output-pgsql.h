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

extern struct output_t out_pgsql;

#endif
