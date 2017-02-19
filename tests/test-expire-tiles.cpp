#include "expire-tiles.hpp"
#include "options.hpp"

#include <iterator>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdexcept>
#include <boost/format.hpp>
#include <set>

#define EARTH_CIRCUMFERENCE (40075016.68)

namespace {

void run_test(const char* test_name, void (*testfunc)())
{
    try
    {
        fprintf(stderr, "%s\n", test_name);
        testfunc();
    }
    catch(const std::exception& e)
    {
        fprintf(stderr, "%s\n", e.what());
        fprintf(stderr, "FAIL\n");
        exit(EXIT_FAILURE);
    }
    fprintf(stderr, "PASS\n");
}
#define RUN_TEST(x) run_test(#x, &(x))
#define ASSERT_EQ(a, b) { if (!((a) == (b))) { throw std::runtime_error((boost::format("Expecting %1% == %2%, but %3% != %4%") % #a % #b % (a) % (b)).str()); } }

struct xyz {
    int z;
    int64_t x, y;
    xyz(int z_, int64_t x_, int64_t y_) : z(z_), x(x_), y(y_) {}
    bool operator==(const xyz &other) const
    {
        return ((z == other.z) && (x == other.x) && (y == other.y));
  }
  bool operator<(const xyz &other) const {
    return ((z < other.z) ||
            ((z == other.z) &&
             ((x < other.x) ||
              ((x == other.x) &&
               (y < other.y)))));
  }
  void to_bbox(double &x0, double &y0,
               double &x1, double &y1) const {
    const double datum = 0.5 * (1 << z);
    const double scale = EARTH_CIRCUMFERENCE / (1 << z);
    x0 = (x - datum) * scale;
    y0 = (datum - (y + 1)) * scale;
    x1 = ((x + 1) - datum) * scale;
    y1 = (datum - y) * scale;
  }
  void to_centroid(double &x0, double &y0) const {
    const double datum = 0.5 * (1 << z);
    const double scale = EARTH_CIRCUMFERENCE / (1 << z);
    x0 = ((x + 0.5) - datum) * scale;
    y0 = (datum - (y + 0.5)) * scale;
  }
};

static std::shared_ptr<reprojection> defproj(reprojection::create_projection(PROJ_SPHERE_MERC));

std::ostream &operator<<(std::ostream &out, const xyz &tile) {
  out << tile.z << "/" << tile.x << "/" << tile.y;
  return out;
}

struct tile_output_set
{
  tile_output_set(int min) : min_zoom(min) {}

  ~tile_output_set() = default;

  void output_dirty_tile(int64_t x, int64_t y, int zoom)
  {
      m_tiles.insert(xyz(zoom, x, y));
  }

  std::set<xyz> m_tiles;
  int min_zoom;
};

void test_xy_to_quadtree_z3()
{
    int64_t qt_expected = 0b100111;
    int64_t qt2 = expire_tiles::xy_to_quadtree(3, 5, 3);
    ASSERT_EQ(qt2, qt_expected);
    xy_coord_t xy = expire_tiles::quadtree_to_xy(qt_expected, 3);
    ASSERT_EQ(xy.x, 3);
    ASSERT_EQ(xy.y, 5);
}

void test_xy_to_quadtree_z16()
{
    int64_t qt_expected = 0b11111111111111111111111111111111;
    int64_t qt2 = expire_tiles::xy_to_quadtree(65535, 65535, 16);
    ASSERT_EQ(qt2, qt_expected);
    xy_coord_t xy = expire_tiles::quadtree_to_xy(qt_expected, 16);
    ASSERT_EQ(xy.x, 65535);
    ASSERT_EQ(xy.y, 65535);
}

/**
 * This test prevents problems which occur if 32-bit integers are used
 * instead of 64-bit integers.
 */
void test_xy_to_quadtree_z18()
{
    int64_t qt_expected = 0b111111111111111111111111111111111111;
    int64_t qt2 = expire_tiles::xy_to_quadtree(262143, 262143, 18);
    ASSERT_EQ(qt2, qt_expected);
    xy_coord_t xy = expire_tiles::quadtree_to_xy(qt_expected, 18);
    ASSERT_EQ(xy.x, 262143);
    ASSERT_EQ(xy.y, 262143);
    qt_expected = 0b001111111111111111111111111111110000;
    qt2 = expire_tiles::xy_to_quadtree(131068, 131068, 18);
    ASSERT_EQ(qt2, qt_expected);
    xy = expire_tiles::quadtree_to_xy(qt_expected, 18);
    ASSERT_EQ(xy.x, 131068);
    ASSERT_EQ(xy.y, 131068);
}

void test_expire_simple_z1() {
    int minzoom = 1;
    expire_tiles et(minzoom, 20000, defproj);
    tile_output_set set(minzoom);

    // as big a bbox as possible at the origin to dirty all four
    // quadrants of the world.
    et.from_bbox(-10000, -10000, 10000, 10000);
    et.output_and_destroy<tile_output_set>(set, minzoom);

    ASSERT_EQ(set.m_tiles.size(), 4);
    std::set<xyz>::iterator itr = set.m_tiles.begin();
    ASSERT_EQ(*itr, xyz(1, 0, 0));
    ++itr;
    ASSERT_EQ(*itr, xyz(1, 0, 1));
    ++itr;
    ASSERT_EQ(*itr, xyz(1, 1, 0));
    ++itr;
    ASSERT_EQ(*itr, xyz(1, 1, 1));
    ++itr;
}

void test_expire_simple_z3() {
    int minzoom = 3;
    expire_tiles et(minzoom, 20000, defproj);
    tile_output_set set(minzoom);

    // as big a bbox as possible at the origin to dirty all four
    // quadrants of the world.
    et.from_bbox(-10000, -10000, 10000, 10000);
    et.output_and_destroy<tile_output_set>(set, minzoom);

    ASSERT_EQ(set.m_tiles.size(), 4);
    std::set<xyz>::iterator itr = set.m_tiles.begin();
    ASSERT_EQ(*itr, xyz(3, 3, 3));
    ++itr;
    ASSERT_EQ(*itr, xyz(3, 3, 4));
    ++itr;
    ASSERT_EQ(*itr, xyz(3, 4, 3));
    ++itr;
    ASSERT_EQ(*itr, xyz(3, 4, 4));
    ++itr;
}

void test_expire_simple_z18() {
    int minzoom = 18;
    expire_tiles et(18, 20000, defproj);
    tile_output_set set(minzoom);

    // dirty a smaller bbox this time, as at z18 the scale is
    // pretty small.
    et.from_bbox(-1, -1, 1, 1);
    et.output_and_destroy(set, minzoom);

    ASSERT_EQ(set.m_tiles.size(), 4);
    std::set<xyz>::iterator itr = set.m_tiles.begin();
    ASSERT_EQ(*itr, xyz(18, 131071, 131071));
    ++itr;
    ASSERT_EQ(*itr, xyz(18, 131071, 131072));
    ++itr;
    ASSERT_EQ(*itr, xyz(18, 131072, 131071));
    ++itr;
    ASSERT_EQ(*itr, xyz(18, 131072, 131072));
    ++itr;
}

std::set<xyz> generate_random(int zoom, size_t count) {
  size_t num = 0;
  std::set<xyz> set;
  const int coord_mask = (1 << zoom) - 1;

  while (num < count) {
    xyz item(zoom, rand() & coord_mask, rand() & coord_mask);
    if (set.count(item) == 0) {
      set.insert(item);
      ++num;
    }
  }

  return set;
}

void assert_tilesets_equal(const std::set<xyz> &a,
                           const std::set<xyz> &b) {
  ASSERT_EQ(a.size(), b.size());
  std::set<xyz>::const_iterator a_itr = a.begin();
  std::set<xyz>::const_iterator b_itr = b.begin();
  while ((a_itr != a.end()) &&
         (b_itr != b.end())) {
    ASSERT_EQ(*a_itr, *b_itr);
    ++a_itr;
    ++b_itr;
  }
}

void expire_centroids(const std::set<xyz> &check_set,
                      expire_tiles &et) {
  for (std::set<xyz>::const_iterator itr = check_set.begin();
       itr != check_set.end(); ++itr) {
    double x0 = 0.0, y0 = 0.0;
    itr->to_centroid(x0, y0);
    et.from_bbox(x0, y0, x0, y0);
  }
}

// tests that expiring a set of tile centroids means that
// those tiles get expired.
void test_expire_set() {
  int zoom = 18;
  for (int i = 0; i < 100; ++i) {
    expire_tiles et(zoom, 20000, defproj);
    tile_output_set set(zoom);

    std::set<xyz> check_set = generate_random(zoom, 100);
    expire_centroids(check_set, et);

    et.output_and_destroy(set, zoom);

    assert_tilesets_equal(set.m_tiles, check_set);
  }
}

// this tests that, after expiring a random set of tiles
// in one expire_tiles object and a different set in
// another, when they are merged together they are the
// same as if the union of the sets of tiles had been
// expired.
void test_expire_merge() {
  int zoom = 18;

  for (int i = 0; i < 100; ++i) {
    expire_tiles et(zoom, 20000, defproj);
    expire_tiles et1(zoom, 20000, defproj);
    expire_tiles et2(zoom, 20000, defproj);
    tile_output_set set(zoom);

    std::set<xyz> check_set1 = generate_random(zoom, 100);
    expire_centroids(check_set1, et1);

    std::set<xyz> check_set2 = generate_random(zoom, 100);
    expire_centroids(check_set2, et2);

    et.merge_and_destroy(et1);
    et.merge_and_destroy(et2);

    std::set<xyz> check_set;
    std::set_union(check_set1.begin(), check_set1.end(),
                   check_set2.begin(), check_set2.end(),
                   std::inserter(check_set, check_set.end()));

    et.output_and_destroy(set, zoom);

    assert_tilesets_equal(set.m_tiles, check_set);
  }
}

// tests that merging two identical sets results in
// the same set. this guarantees that we check some
// pathways of the merging which possibly could be
// skipped by the random tile set in the previous
// test.
void test_expire_merge_same() {
  int zoom = 18;

  for (int i = 0; i < 100; ++i) {
    expire_tiles et(zoom, 20000, defproj);
    expire_tiles et1(zoom, 20000, defproj);
    expire_tiles et2(zoom, 20000, defproj);
    tile_output_set set(zoom);

    std::set<xyz> check_set = generate_random(zoom, 100);
    expire_centroids(check_set, et1);
    expire_centroids(check_set, et2);

    et.merge_and_destroy(et1);
    et.merge_and_destroy(et2);

    et.output_and_destroy(set, zoom);

    assert_tilesets_equal(set.m_tiles, check_set);
  }
}

// makes sure that we're testing the case where some
// tiles are in both.
void test_expire_merge_overlap() {
  int zoom = 18;

  for (int i = 0; i < 100; ++i) {
    expire_tiles et(zoom, 20000, defproj);
    expire_tiles et1(zoom, 20000, defproj);
    expire_tiles et2(zoom, 20000, defproj);
    tile_output_set set(zoom);

    std::set<xyz> check_set1 = generate_random(zoom, 100);
    expire_centroids(check_set1, et1);

    std::set<xyz> check_set2 = generate_random(zoom, 100);
    expire_centroids(check_set2, et2);

    std::set<xyz> check_set3 = generate_random(zoom, 100);
    expire_centroids(check_set3, et1);
    expire_centroids(check_set3, et2);

    et.merge_and_destroy(et1);
    et.merge_and_destroy(et2);

    std::set<xyz> check_set;
    std::set_union(check_set1.begin(), check_set1.end(),
                   check_set2.begin(), check_set2.end(),
                   std::inserter(check_set, check_set.end()));
    std::set_union(check_set1.begin(), check_set1.end(),
                   check_set3.begin(), check_set3.end(),
                   std::inserter(check_set, check_set.end()));

    et.output_and_destroy(set, zoom);

    assert_tilesets_equal(set.m_tiles, check_set);
  }
}

// checks that the set union still works when we expire
// large contiguous areas of tiles (i.e: ensure that we
// handle the "complete" flag correctly).
void test_expire_merge_complete() {
  int zoom = 18;

  for (int i = 0; i < 100; ++i) {
    expire_tiles et(zoom, 20000, defproj);
    expire_tiles et0(zoom, 20000, defproj);
    expire_tiles et1(zoom, 20000, defproj);
    expire_tiles et2(zoom, 20000, defproj);
    tile_output_set set(zoom);
    tile_output_set set0(zoom);

    // et1&2 are two halves of et0's box
    et0.from_bbox(-10000, -10000, 10000, 10000);
    et1.from_bbox(-10000, -10000,     0, 10000);
    et2.from_bbox(     0, -10000, 10000, 10000);

    et.merge_and_destroy(et1);
    et.merge_and_destroy(et2);

    et.output_and_destroy(set, zoom);
    et0.output_and_destroy(set0, zoom);

    assert_tilesets_equal(set.m_tiles, set0.m_tiles);
  }
}

} // anonymous namespace

int main(int argc, char *argv[])
{
    srand(0);

    //try each test if any fail we will exit
    RUN_TEST(test_xy_to_quadtree_z3);
    RUN_TEST(test_xy_to_quadtree_z16);
    RUN_TEST(test_xy_to_quadtree_z18);
    RUN_TEST(test_expire_simple_z1);
    RUN_TEST(test_expire_simple_z3);
    RUN_TEST(test_expire_simple_z18);
    RUN_TEST(test_expire_set);
    RUN_TEST(test_expire_merge);
    RUN_TEST(test_expire_merge_same);
    RUN_TEST(test_expire_merge_overlap);
    RUN_TEST(test_expire_merge_complete);

    //passed
    return 0;
}
