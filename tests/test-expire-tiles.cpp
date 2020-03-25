#include <catch.hpp>

#include <set>

#include "expire-tiles.hpp"
#include "reprojection.hpp"

static constexpr double EARTH_CIRCUMFERENCE = 40075016.68;
static std::shared_ptr<reprojection>
    defproj(reprojection::create_projection(PROJ_SPHERE_MERC));

class xyz
{
public:
    xyz(uint32_t z_, int64_t x_, int64_t y_) : z(z_), x(x_), y(y_) {}

    bool operator==(xyz const &other) const noexcept
    {
        return ((z == other.z) && (x == other.x) && (y == other.y));
    }

    bool operator<(xyz const &other) const noexcept
    {
        if (z < other.z) {
            return true;
        }
        if (z > other.z) {
            return false;
        }
        if (x < other.x) {
            return true;
        }
        if (x > other.x) {
            return false;
        }
        if (y < other.y) {
            return true;
        }

        return false;
    }

    void to_centroid(double &cx, double &cy) const noexcept
    {
        double const datum = 0.5 * (1U << z);
        double const scale = EARTH_CIRCUMFERENCE / (1U << z);
        cx = ((x + 0.5) - datum) * scale;
        cy = (datum - (y + 0.5)) * scale;
    }

private:
    uint32_t z;
    int64_t x, y;
};

struct tile_output_set
{
    bool operator==(tile_output_set const &other) const
    {
        return tiles == other.tiles;
    }

    tile_output_set &operator+=(tile_output_set const &other)
    {
        tiles.insert(other.tiles.cbegin(), other.tiles.cend());
        return *this;
    }

    void output_dirty_tile(int64_t x, int64_t y, uint32_t zoom)
    {
        tiles.insert(xyz{zoom, x, y});
    }

    void expire_centroids(expire_tiles &et)
    {
        for (auto const &t : tiles) {
            double x0, y0;
            t.to_centroid(x0, y0);
            et.from_bbox(x0, y0, x0, y0);
        }
    }

    static tile_output_set generate_random(uint32_t zoom, size_t count)
    {
        tile_output_set set;
        int const cmask = (1 << zoom) - 1;

        do {
            set.output_dirty_tile(rand() & cmask, rand() & cmask, zoom);
        } while (set.tiles.size() < count);

        return set;
    }

    std::set<xyz> tiles;
};

void check_quadkey_to_xy(uint64_t quadkey_expected, uint32_t x, uint32_t y,
                         uint32_t zoom)
{
    CHECK(expire_tiles::xy_to_quadkey(x, y, zoom) == quadkey_expected);

    xy_coord_t xy = expire_tiles::quadkey_to_xy(quadkey_expected, zoom);
    CHECK(xy.x == x);
    CHECK(xy.y == y);
}

TEST_CASE("xy to quadkey", "[NoDB]")
{
    check_quadkey_to_xy(0x27, 3, 5, 3);
    check_quadkey_to_xy(0xffffffff, 65535, 65535, 16);
    // This test prevents problems which occur if 32-bit integers are used
    // instead of 64-bit integers.
    check_quadkey_to_xy(0xfffffffff, 262143, 262143, 18);
    check_quadkey_to_xy(0x3fffffff0, 131068, 131068, 18);
}

TEST_CASE("simple expire z1", "[NoDB]")
{
    uint32_t minzoom = 1;
    expire_tiles et(minzoom, 20000, defproj);

    // as big a bbox as possible at the origin to dirty all four
    // quadrants of the world.
    et.from_bbox(-10000, -10000, 10000, 10000);
    tile_output_set set;
    et.output_and_destroy(set, minzoom);

    CHECK(set.tiles.size() == 4);

    auto itr = set.tiles.begin();
    CHECK(*(itr++) == xyz(1, 0, 0));
    CHECK(*(itr++) == xyz(1, 0, 1));
    CHECK(*(itr++) == xyz(1, 1, 0));
    CHECK(*(itr++) == xyz(1, 1, 1));
}

TEST_CASE("simple expire z3", "[NoDB]")
{
    uint32_t minzoom = 3;
    expire_tiles et(minzoom, 20000, defproj);

    // as big a bbox as possible at the origin to dirty all four
    // quadrants of the world.
    et.from_bbox(-10000, -10000, 10000, 10000);
    tile_output_set set;
    et.output_and_destroy(set, minzoom);

    CHECK(set.tiles.size() == 4);

    auto itr = set.tiles.begin();
    CHECK(*(itr++) == xyz(3, 3, 3));
    CHECK(*(itr++) == xyz(3, 3, 4));
    CHECK(*(itr++) == xyz(3, 4, 3));
    CHECK(*(itr++) == xyz(3, 4, 4));
}

TEST_CASE("simple expire z18", "[NoDB]")
{
    uint32_t minzoom = 18;
    expire_tiles et(18, 20000, defproj);

    // dirty a smaller bbox this time, as at z18 the scale is
    // pretty small.
    et.from_bbox(-1, -1, 1, 1);
    tile_output_set set;
    et.output_and_destroy(set, minzoom);

    CHECK(set.tiles.size() == 4);

    auto itr = set.tiles.begin();
    CHECK(*(itr++) == xyz(18, 131071, 131071));
    CHECK(*(itr++) == xyz(18, 131071, 131072));
    CHECK(*(itr++) == xyz(18, 131072, 131071));
    CHECK(*(itr++) == xyz(18, 131072, 131072));
}

/**
 * Test tile expiry on two zoom levels.
 */
TEST_CASE("simple expire z17 and z18", "[NoDB]")
{
    uint32_t minzoom = 17;
    expire_tiles et(18, 20000, defproj);

    // dirty a smaller bbox this time, as at z18 the scale is
    // pretty small.
    et.from_bbox(-1, -1, 1, 1);
    tile_output_set set;
    et.output_and_destroy(set, minzoom);

    CHECK(set.tiles.size() == 8);

    auto itr = set.tiles.begin();
    CHECK(*(itr++) == xyz(17, 65535, 65535));
    CHECK(*(itr++) == xyz(17, 65535, 65536));
    CHECK(*(itr++) == xyz(17, 65536, 65535));
    CHECK(*(itr++) == xyz(17, 65536, 65536));
    CHECK(*(itr++) == xyz(18, 131071, 131071));
    CHECK(*(itr++) == xyz(18, 131071, 131072));
    CHECK(*(itr++) == xyz(18, 131072, 131071));
    CHECK(*(itr++) == xyz(18, 131072, 131072));
}

/**
 * Similar to test_expire_simple_z17_18 but now all z18 tiles are children
 * of the same z17 tile.
 */
TEST_CASE("simple expire z17 and z18 in one superior tile", "[NoDB]")
{
    uint32_t minzoom = 17;
    expire_tiles et(18, 20000, defproj);

    et.from_bbox(-163, 140, -140, 164);
    tile_output_set set;
    et.output_and_destroy(set, minzoom);

    CHECK(set.tiles.size() == 5);

    auto itr = set.tiles.begin();

    CHECK(*(itr++) == xyz(17, 65535, 65535));
    CHECK(*(itr++) == xyz(18, 131070, 131070));
    CHECK(*(itr++) == xyz(18, 131070, 131071));
    CHECK(*(itr++) == xyz(18, 131071, 131070));
    CHECK(*(itr++) == xyz(18, 131071, 131071));
}

/**
 * Expiring a set of tile centroids means that those tiles get expired.
 */
TEST_CASE("expire centroids", "[NoDB]")
{
    uint32_t zoom = 18;
    for (int i = 0; i < 100; ++i) {
        expire_tiles et(zoom, 20000, defproj);

        auto check_set = tile_output_set::generate_random(zoom, 100);
        check_set.expire_centroids(et);

        tile_output_set set;
        et.output_and_destroy(set, zoom);

        CHECK(set == check_set);
    }
}

/**
 * After expiring a random set of tiles in one expire_tiles object
 * and a different set in another, when they are merged together they are the
 * same as if the union of the sets of tiles had been expired.
 */
TEST_CASE("merge expire sets", "[NoDB]")
{
    uint32_t zoom = 18;

    for (int i = 0; i < 100; ++i) {
        expire_tiles et(zoom, 20000, defproj);
        expire_tiles et1(zoom, 20000, defproj);
        expire_tiles et2(zoom, 20000, defproj);

        auto check_set1 = tile_output_set::generate_random(zoom, 100);
        check_set1.expire_centroids(et1);

        auto check_set2 = tile_output_set::generate_random(zoom, 100);
        check_set2.expire_centroids(et2);

        et.merge_and_destroy(et1);
        et.merge_and_destroy(et2);

        tile_output_set check_set;
        check_set += check_set1;
        check_set += check_set2;

        tile_output_set set;
        et.output_and_destroy(set, zoom);

        CHECK(set == check_set);
    }
}

/**
 * Merging two identical sets results in the same set. This guarantees that
 * we check some pathways of the merging which possibly could be skipped by
 * the random tile set in the previous test.
 */
TEST_CASE("merge identical expire sets", "[NoDB]")
{
    uint32_t zoom = 18;

    for (int i = 0; i < 100; ++i) {
        expire_tiles et(zoom, 20000, defproj);
        expire_tiles et1(zoom, 20000, defproj);
        expire_tiles et2(zoom, 20000, defproj);

        auto check_set = tile_output_set::generate_random(zoom, 100);
        check_set.expire_centroids(et1);
        check_set.expire_centroids(et2);

        et.merge_and_destroy(et1);
        et.merge_and_destroy(et2);

        tile_output_set set;
        et.output_and_destroy(set, zoom);

        CHECK(set == check_set);
    }
}

/**
 * Make sure that we're testing the case where some tiles are in both.
 */
TEST_CASE("merge overlapping expire sets", "[NoDB]")
{
    uint32_t zoom = 18;

    for (int i = 0; i < 100; ++i) {
        expire_tiles et(zoom, 20000, defproj);
        expire_tiles et1(zoom, 20000, defproj);
        expire_tiles et2(zoom, 20000, defproj);

        auto check_set1 = tile_output_set::generate_random(zoom, 100);
        check_set1.expire_centroids(et1);

        auto check_set2 = tile_output_set::generate_random(zoom, 100);
        check_set2.expire_centroids(et2);

        auto check_set3 = tile_output_set::generate_random(zoom, 100);
        check_set3.expire_centroids(et1);
        check_set3.expire_centroids(et2);

        et.merge_and_destroy(et1);
        et.merge_and_destroy(et2);

        tile_output_set check_set;
        check_set += check_set1;
        check_set += check_set2;
        check_set += check_set3;

        tile_output_set set;
        et.output_and_destroy(set, zoom);

        CHECK(set == check_set);
    }
}

/**
 * The set union still works when we expire large contiguous areas of tiles
 * (i.e: ensure that we handle the "complete" flag correctly)
 */
TEST_CASE("merge with complete flag", "[NoDB]")
{
    uint32_t zoom = 18;

    expire_tiles et(zoom, 20000, defproj);
    expire_tiles et0(zoom, 20000, defproj);
    expire_tiles et1(zoom, 20000, defproj);
    expire_tiles et2(zoom, 20000, defproj);

    // et1&2 are two halves of et0's box
    et0.from_bbox(-10000, -10000, 10000, 10000);
    et1.from_bbox(-10000, -10000, 0, 10000);
    et2.from_bbox(0, -10000, 10000, 10000);

    et.merge_and_destroy(et1);
    et.merge_and_destroy(et2);

    tile_output_set set;
    et.output_and_destroy(set, zoom);
    tile_output_set set0;
    et0.output_and_destroy(set0, zoom);

    CHECK(set == set0);
}
