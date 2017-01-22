#ifndef EXPIRE_TILES_H
#define EXPIRE_TILES_H

#include <memory>

#include "osmtypes.hpp"

class reprojection;
class table_t;
class tile;
class ewkb_parser_t;

struct expire_tiles
{
    expire_tiles(int maxzoom, double maxbbox,
                 const std::shared_ptr<reprojection> &projection);

    int from_bbox(double min_lon, double min_lat, double max_lon, double max_lat);
    void from_nodes_line(const nodelist_t &nodes);
    void from_nodes_poly(const nodelist_t &nodes, osmid_t osm_id);
    void from_wkb(const char* wkb, osmid_t osm_id);
    int from_db(table_t* table, osmid_t osm_id);

    /* customisable tile output. this can be passed into the
     * `output_and_destroy` function to override output to a file.
     * this is primarily useful for testing.
     */
    struct tile_output {
        virtual ~tile_output() = default;
        // dirty a tile at x, y & zoom, and all descendants of that
        // tile at the given zoom if zoom < min_zoom.
        virtual void output_dirty_tile(int x, int y, int zoom) = 0;
    };

    // output the list of expired tiles to a file. note that this
    // consumes the list of expired tiles destructively.
    void output_and_destroy(const char *filename, int minzoom);

    // output the list of expired tiles using a `tile_output`
    // functor. this consumes the list of expired tiles destructively.
    void output_and_destroy(tile_output *output);

    // merge the list of expired tiles in the other object into this
    // object, destroying the list in the other object.
    void merge_and_destroy(expire_tiles &);

private:
    void expire_tile(int x, int y);
    int normalise_tile_x_coord(int x);
    void from_line(double lon_a, double lat_a, double lon_b, double lat_b);

    void from_wkb_point(ewkb_parser_t *wkb);
    void from_wkb_line(ewkb_parser_t *wkb);
    void from_wkb_polygon(ewkb_parser_t *wkb, osmid_t osm_id);

    double tile_width;
    double max_bbox;
    int map_width;
    int maxzoom;
    std::shared_ptr<reprojection> projection;
    std::unique_ptr<tile> dirty;
};


class tile
{
public:
    int mark_tile(int x, int y, int zoom, int this_zoom);
    void output_and_destroy(expire_tiles::tile_output *output,
                            int x, int y, int this_zoom);
    int merge(tile *other);

private:
    int sub2x(int sub) const { return sub >> 1; }
    int sub2y(int sub) const { return sub & 1; }

    int num_complete() const
    {
        return complete[0] + complete[1] + complete[2] + complete[3];
    }

    std::unique_ptr<tile> subtiles[4];
    char complete[4] = {0, 0, 0, 0};
};

#endif
