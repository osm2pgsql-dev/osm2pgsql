#ifndef EXPIRE_TILES_H
#define EXPIRE_TILES_H

#include "output.h"

void expire_tiles_init(const struct output_options *options);
void expire_tiles_stop(void);
int expire_tiles_from_bbox(double min_lon, double min_lat, double max_lon, double max_lat);
void expire_tiles_from_nodes_line(struct osmNode * nodes, int count);
void expire_tiles_from_nodes_poly(struct osmNode * nodes, int count, osmid_t osm_id);
void expire_tiles_from_wkt(const char * wkt, osmid_t osm_id);
int expire_tiles_from_db(PGconn * sql_conn, osmid_t osm_id);

#endif
