#include "expire-tiles.hpp"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdexcept>
#include <boost/format.hpp>

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

}

int main(int argc, char *argv[])
{
    srand(0);

    //try each test if any fail we will exit
    RUN_TEST(test_expire_simple_z1);
    RUN_TEST(test_expire_simple_z3);
    RUN_TEST(test_expire_simple_z18);

    //passed
    return 0;
}
