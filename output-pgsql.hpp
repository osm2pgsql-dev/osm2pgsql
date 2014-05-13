/* Implements the output-layer processing for osm2pgsql
 * storing the data in several PostgreSQL tables
 * with the final PostGIS geometries for each entity
*/
 
#ifndef OUTPUT_PGSQL_H
#define OUTPUT_PGSQL_H

#include "output.hpp"

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

struct output_pgsql_t : public output_t {
    output_pgsql_t();
    virtual ~output_pgsql_t();

    int start(const struct output_options *options);
    int connect(const struct output_options *options, int startTransaction);
    void stop();
    void cleanup(void);
    void close(int stopTransaction);

    int node_add(osmid_t id, double lat, double lon, struct keyval *tags);
    int way_add(osmid_t id, osmid_t *nodes, int node_count, struct keyval *tags);
    int relation_add(osmid_t id, struct member *members, int member_count, struct keyval *tags);

    int node_modify(osmid_t id, double lat, double lon, struct keyval *tags);
    int way_modify(osmid_t id, osmid_t *nodes, int node_count, struct keyval *tags);
    int relation_modify(osmid_t id, struct member *members, int member_count, struct keyval *tags);

    int node_delete(osmid_t id);
    int way_delete(osmid_t id);
    int relation_delete(osmid_t id);
};

extern output_pgsql_t out_pgsql;

#endif
