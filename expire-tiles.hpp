#ifndef EXPIRE_TILES_H
#define EXPIRE_TILES_H

#include "output.hpp"
#include "reprojection.hpp"
#include <boost/noncopyable.hpp>
#include <boost/shared_ptr.hpp>

struct expire_tiles : public boost::noncopyable {
    explicit expire_tiles(const struct output_options *options);
    ~expire_tiles();

    int from_bbox(double min_lon, double min_lat, double max_lon, double max_lat);
    void from_nodes_line(struct osmNode * nodes, int count);
    void from_nodes_poly(struct osmNode * nodes, int count, osmid_t osm_id);
    void from_wkt(const char * wkt, osmid_t osm_id);
    int from_db(struct pg_conn * sql_conn, osmid_t osm_id);

    struct tile {
	int		complete[2][2];	/* Flags */
	struct tile *	subtiles[2][2];
    };

private: 
    void expire_tile(int x, int y);
    int normalise_tile_x_coord(int x);
    void from_line(double lon_a, double lat_a, double lon_b, double lat_b);
    void from_xnodes_poly(struct osmNode ** xnodes, int * xcount, osmid_t osm_id);
    void from_xnodes_line(struct osmNode ** xnodes, int * xcount);
    void output_and_destroy_tree(FILE * outfile, struct tile * tree);

    int map_width;
    double tile_width;
    const struct output_options *Options;
    struct tile *dirty;
    int outcount;

    boost::shared_ptr<reprojection> reproj;
};

#endif
