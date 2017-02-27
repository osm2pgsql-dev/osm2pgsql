#ifndef EXPIRE_TILES_H
#define EXPIRE_TILES_H

#include <memory>
#include <unordered_set>

#include "osmtypes.hpp"

class reprojection;
class table_t;
class tile;
namespace ewkb {
class parser_t;
}

/**
 * \brief Simple struct for a method converting a quadtree node ID to a pair of a x and y coordinate.
 *
 * If we used std::pair<int, int> instead of this type, we would have to write foo.first instead
 * of foo.x. The latter one is easier to understand, isn't it?
 */
struct xy_coord_t
{
    int x;
    int y;
    xy_coord_t() : x(0), y(0) {}
};

/**
 * This struct is handed over as template parameter to output_and_destroy
 * method.
 */
class tile_output_t
{
    FILE *outfile;

public:
    tile_output_t(const char *filename);

    ~tile_output_t();

    /**
     * \brief output dirty tile
     *
     * \param x x index
     * \param y y index
     * \param zoom zoom level of the tile
     */
    void output_dirty_tile(int x, int y, int zoom);
};

struct expire_tiles
{
    expire_tiles(int maxzoom, double maxbbox,
                 const std::shared_ptr<reprojection> &projection);

    int from_bbox(double min_lon, double min_lat, double max_lon, double max_lat);
    void from_wkb(const char* wkb, osmid_t osm_id);
    int from_db(table_t* table, osmid_t osm_id);

    /**
     * \brief Write the list of expired tiles to a file.
     *
     * You will probably use tile_output_t as template argument for production code
     * and another class which does not write to a file for unit tests.
     *
     * \param filename name of the file
     * \param minzoom minimum zoom level
     */
    void output_and_destroy(const char *filename, int minzoom);

    /**
     * \brief Output expried tiles on all requested zoom levels.
     *
     * \param output_writer class which implements the method
     * output_dirty_tile(int x, int y, int zoom) which usually writes the tile ID to a file
     * (production code) or does something else (usually unit tests)
     *
     * \param minzoom minimum zoom level
     */
    template <class tile_writer_t>
    void output_and_destroy(tile_writer_t &output_writer, int minzoom)
    {
        /* Loop over all requested zoom levels (from maximum down to the minimum zoom level).
         * Tile IDs of the tiles enclosing this tile at lower zoom levels are calculated using
         * bit shifts.
         */
        for (int dz = 0; dz <= maxzoom - minzoom; dz++) {
            // track which tiles have already been written
            std::unordered_set<int64_t> expired_tiles;
            // iterate over all expired tiles
            for (std::unordered_set<int64_t>::iterator it = m_dirty_tiles.begin();
                    it != m_dirty_tiles.end(); it++) {
                int64_t qt_new = *it >> (dz * 2);
                if (expired_tiles.insert(qt_new).second) {
                    // expired_tiles.insert(qt_new).second is true if the tile has not been written to the list yet
                    xy_coord_t xy = quadtree_to_xy(qt_new, maxzoom - dz);
                    output_writer.output_dirty_tile(xy.x, xy.y, maxzoom - dz);
                }
            }
        }
    }

    /**
    * merge the list of expired tiles in the other object into this
    * object, destroying the list in the other object.
    */
    void merge_and_destroy(expire_tiles &other);

    /**
     * \brief Helper method to convert a tile ID (x and y) into quadtree
     * coordinate using bitshifts.
     *
     * Quadtree coordinates are interleaved this way: YXYX…
     *
     * \param x x index
     * \param y y index
     * \param zoom zoom level
     * \returns quadtree ID as integer
     */
    static int64_t xy_to_quadtree(int x, int y, int zoom);

    /**
     * \brief Convert a quadtree coordinate into a tile ID (x and y) using bitshifts.
     *
     * Quadtree coordinates are interleaved this way: YXYX…
     *
     * \param qt_coord quadtree ID
     * \param zoom zoom level
     */
    static xy_coord_t quadtree_to_xy(int64_t qt_coord, int zoom);

private:
    /**
     * Expire a single tile.
     *
     * \param x x index of the tile to be expired.
     * \param y y index of the tile to be expired.
     */
    void expire_tile(int x, int y);
    int normalise_tile_x_coord(int x);
    void from_line(double lon_a, double lat_a, double lon_b, double lat_b);

    void from_wkb_point(ewkb::parser_t *wkb);
    void from_wkb_line(ewkb::parser_t *wkb);
    void from_wkb_polygon(ewkb::parser_t *wkb, osmid_t osm_id);

    double tile_width;
    double max_bbox;
    int map_width;
    int maxzoom;
    std::shared_ptr<reprojection> projection;

    /**
     * \brief manages which tiles have been marked as empty
     *
     * This set stores the IDs of the tiles at the maximum zoom level. We don't
     * store the IDs of the expired tiles of lower zoom levels. They are calculated
     * on the fly at the end.
     *
     * Tile IDs are converted into so-called quadkeys as used by Bing Maps.
     * https://msdn.microsoft.com/en-us/library/bb259689.aspx
     * A quadkey is generated by interleaving the x and y index in following order:
     * YXYX...
     *
     * Example:
     * x = 3 = 0b011, y = 5 = 0b101
     * results in the quadkey 0b100111.
     *
     * Bing Maps itself uses the quadkeys as a base-4 number converted to a string.
     * We interpret this IDs as simple 64-bit integers due to performance reasons.
     */
    std::unordered_set<int64_t> m_dirty_tiles;
};

#endif
