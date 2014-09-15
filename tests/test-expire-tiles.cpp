#include "expire-tiles.hpp"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdexcept>
#include <boost/format.hpp>

#define EARTH_CIRCUMFERENCE (40075016.68)

namespace {

void run_test(const char* test_name, void (*testfunc)())
{
    try
    {
        fprintf(stderr, "%s\n", test_name);
        testfunc();
    }
    catch(std::exception& e)
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
  int z, x, y;
  xyz(int z_, int x_, int y_) : z(z_), x(x_), y(y_) {}
  bool operator==(const xyz &other) const {
    return ((z == other.z) &&
            (x == other.x) &&
            (y == other.y));
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

std::ostream &operator<<(std::ostream &out, const xyz &tile) {
  out << tile.z << "/" << tile.x << "/" << tile.y;
  return out;
}

struct tile_output_set : public expire_tiles::tile_output {
  tile_output_set() {}

  virtual ~tile_output_set() {}

  virtual void output_dirty_tile(int x, int y, int zoom, int min_zoom) {
    int	y_min, x_iter, y_iter, x_max, y_max, out_zoom, zoom_diff;

    if (zoom > min_zoom) out_zoom = zoom;
    else out_zoom = min_zoom;
    zoom_diff = out_zoom - zoom;
    y_min = y << zoom_diff;
    x_max = (x + 1) << zoom_diff;
    y_max = (y + 1) << zoom_diff;
    for (x_iter = x << zoom_diff; x_iter < x_max; x_iter++) {
      for (y_iter = y_min; y_iter < y_max; y_iter++) {
        m_tiles.insert(xyz(out_zoom, x_iter, y_iter));
      }
    }
  }

  std::set<xyz> m_tiles;
};

void test_expire_simple_z1() {
  options_t opt;
  opt.expire_tiles_zoom = 1;
  opt.expire_tiles_zoom_min = 1;

  expire_tiles et(&opt);
  tile_output_set set;

  // as big a bbox as possible at the origin to dirty all four
  // quadrants of the world.
  et.from_bbox(-10000, -10000, 10000, 10000);
  et.output_and_destroy(&set);

  ASSERT_EQ(set.m_tiles.size(), 4);
  std::set<xyz>::iterator itr = set.m_tiles.begin();
  ASSERT_EQ(*itr, xyz(1, 0, 0)); ++itr;
  ASSERT_EQ(*itr, xyz(1, 0, 1)); ++itr;
  ASSERT_EQ(*itr, xyz(1, 1, 0)); ++itr;
  ASSERT_EQ(*itr, xyz(1, 1, 1)); ++itr;
}

void test_expire_simple_z3() {
  options_t opt;
  opt.expire_tiles_zoom = 3;
  opt.expire_tiles_zoom_min = 3;

  expire_tiles et(&opt);
  tile_output_set set;

  // as big a bbox as possible at the origin to dirty all four
  // quadrants of the world.
  et.from_bbox(-10000, -10000, 10000, 10000);
  et.output_and_destroy(&set);

  ASSERT_EQ(set.m_tiles.size(), 4);
  std::set<xyz>::iterator itr = set.m_tiles.begin();
  ASSERT_EQ(*itr, xyz(3, 3, 3)); ++itr;
  ASSERT_EQ(*itr, xyz(3, 3, 4)); ++itr;
  ASSERT_EQ(*itr, xyz(3, 4, 3)); ++itr;
  ASSERT_EQ(*itr, xyz(3, 4, 4)); ++itr;
}

void test_expire_simple_z18() {
  options_t opt;
  opt.expire_tiles_zoom = 18;
  opt.expire_tiles_zoom_min = 18;

  expire_tiles et(&opt);
  tile_output_set set;

  // dirty a smaller bbox this time, as at z18 the scale is
  // pretty small.
  et.from_bbox(-1, -1, 1, 1);
  et.output_and_destroy(&set);

  ASSERT_EQ(set.m_tiles.size(), 4);
  std::set<xyz>::iterator itr = set.m_tiles.begin();
  ASSERT_EQ(*itr, xyz(18, 131071, 131071)); ++itr;
  ASSERT_EQ(*itr, xyz(18, 131071, 131072)); ++itr;
  ASSERT_EQ(*itr, xyz(18, 131072, 131071)); ++itr;
  ASSERT_EQ(*itr, xyz(18, 131072, 131072)); ++itr;
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

void test_expire_set() {
  options_t opt;
  int zoom = 18;
  opt.expire_tiles_zoom = zoom;
  opt.expire_tiles_zoom_min = zoom;

  for (int i = 0; i < 100; ++i) {
    expire_tiles et(&opt);
    tile_output_set set;

    std::set<xyz> check_set = generate_random(zoom, 100);
    expire_centroids(check_set, et);

    et.output_and_destroy(&set);

    assert_tilesets_equal(set.m_tiles, check_set);
  }
}

} // anonymous namespace

int main(int argc, char *argv[])
{
    srand(0);

    //try each test if any fail we will exit
    RUN_TEST(test_expire_simple_z1);
    RUN_TEST(test_expire_simple_z3);
    RUN_TEST(test_expire_simple_z18);
    RUN_TEST(test_expire_set);

    //passed
    return 0;
}
