#ifndef OSM2PGSQL_EXPIRE_TILES_HPP
#define OSM2PGSQL_EXPIRE_TILES_HPP

#include <memory>
#include <unordered_set>

#include "logging.hpp"
#include "osmtypes.hpp"
#include "pgsql.hpp"

class reprojection;
class table_t;
class tile;
namespace ewkb {
class parser_t;
}

/**
 * \brief Simple struct for the x and y index of a tile ID.
 */
struct xy_coord_t
{
    uint32_t x;
    uint32_t y;
    xy_coord_t() : x(0), y(0) {}
};

/**
 * Implementation of the output of the tile expiry list to a file.
 */
class tile_output_t
{
    FILE *outfile;

public:
    tile_output_t(char const *filename);

    ~tile_output_t();

    /**
     * Output dirty tile.
     *
     * \param x x index
     * \param y y index
     * \param zoom zoom level of the tile
     */
    void output_dirty_tile(uint32_t x, uint32_t y, uint32_t zoom);
};

struct expire_tiles
{
    expire_tiles(uint32_t maxzoom, double maxbbox,
                 const std::shared_ptr<reprojection> &projection);

    int from_bbox(double min_lon, double min_lat, double max_lon,
                  double max_lat);
    void from_wkb(char const *wkb, osmid_t osm_id);
    int from_db(table_t *table, osmid_t osm_id);
    int from_result(pg_result_t const &result, osmid_t osm_id);

    /**
     * Write the list of expired tiles to a file.
     *
     * You will probably use tile_output_t as template argument for production code
     * and another class which does not write to a file for unit tests.
     *
     * \param filename name of the file
     * \param minzoom minimum zoom level
     */
    void output_and_destroy(char const *filename, uint32_t minzoom);

    /**
     * Output expired tiles on all requested zoom levels.
     *
     * \tparam TILE_WRITER class which implements the method
     * output_dirty_tile(uint32_t x, uint32_t y, uint32_t zoom) which usually writes the tile ID to a file
     * (production code) or does something else (usually unit tests)
     *
     * \param minzoom minimum zoom level
     */
    template <class TILE_WRITER>
    void output_and_destroy(TILE_WRITER &output_writer, uint32_t minzoom)
    {
        assert(minzoom <= maxzoom);
        // build a sorted vector of all expired tiles
        std::vector<uint64_t> tiles_maxzoom(m_dirty_tiles.begin(),
                                            m_dirty_tiles.end());
        std::sort(tiles_maxzoom.begin(), tiles_maxzoom.end());
        /* Loop over all requested zoom levels (from maximum down to the minimum zoom level).
         * Tile IDs of the tiles enclosing this tile at lower zoom levels are calculated using
         * bit shifts.
         *
         * last_quadkey is initialized with a value which is not expected to exist
         * (larger than largest possible quadkey). */
        uint64_t last_quadkey = 1ULL << (2 * maxzoom);
        std::size_t count = 0;
        for (std::vector<uint64_t>::const_iterator it = tiles_maxzoom.cbegin();
             it != tiles_maxzoom.cend(); ++it) {
            for (uint32_t dz = 0; dz <= maxzoom - minzoom; ++dz) {
                // scale down to the current zoom level
                uint64_t qt_current = *it >> (dz * 2);
                /* If dz > 0, there are propably multiple elements whose quadkey
                 * is equal because they are all sub-tiles of the same tile at the current
                 * zoom level. We skip all of them after we have written the first sibling.
                 */
                if (qt_current == last_quadkey >> (dz * 2)) {
                    continue;
                }
                xy_coord_t xy = quadkey_to_xy(qt_current, maxzoom - dz);
                output_writer.output_dirty_tile(xy.x, xy.y, maxzoom - dz);
                ++count;
            }
            last_quadkey = *it;
        }
        log_info("Wrote {} entries to expired tiles list", count);
    }

    /**
    * merge the list of expired tiles in the other object into this
    * object, destroying the list in the other object.
    */
    void merge_and_destroy(expire_tiles &other);

    /**
     * Helper method to convert a tile ID (x and y) into a quadkey
     * using bitshifts.
     *
     * Quadkeys are interleaved this way: YXYX…
     *
     * \param x x index
     * \param y y index
     * \param zoom zoom level
     * \returns quadtree ID as integer
     */
    static uint64_t xy_to_quadkey(uint32_t x, uint32_t y, uint32_t zoom);

    /**
     * Convert a quadkey into a tile ID (x and y) using bitshifts.
     *
     * Quadkeys coordinates are interleaved this way: YXYX…
     *
     * \param quadkey the quadkey to be converted
     * \param zoom zoom level
     */
    static xy_coord_t quadkey_to_xy(uint64_t quadkey, uint32_t zoom);

private:

    /**
     * Converts from target coordinates to tile coordinates.
     */
    void coords_to_tile(double lon, double lat, double *tilex, double *tiley);

    /**
     * Expire a single tile.
     *
     * \param x x index of the tile to be expired.
     * \param y y index of the tile to be expired.
     */
    void expire_tile(uint32_t x, uint32_t y);
    int normalise_tile_x_coord(int x);
    void from_line(double lon_a, double lat_a, double lon_b, double lat_b);

    void from_wkb_point(ewkb::parser_t *wkb);
    void from_wkb_line(ewkb::parser_t *wkb);
    void from_wkb_polygon(ewkb::parser_t *wkb, osmid_t osm_id);

    double tile_width;
    double max_bbox;
    int map_width;
    uint32_t maxzoom;
    std::shared_ptr<reprojection> projection;

    /**
     * x coordinate of the tile which has been added as last tile to the unordered set
     */
    uint32_t last_tile_x;

    /**
     * y coordinate of the tile which has been added as last tile to the unordered set
     */
    uint32_t last_tile_y;

    /**
     * manages which tiles have been marked as empty
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
    std::unordered_set<uint64_t> m_dirty_tiles;
};

#endif // OSM2PGSQL_EXPIRE_TILES_HPP
