/*
 * Dirty tile list generation
 *
 * Steve Hill <steve@nexusuk.org>
 *
 * Please refer to the OpenPisteMap expire_tiles.py script for a demonstration
 * of how to make use of the output:
 * https://subversion.nexusuk.org/trac/browser/openpistemap/trunk/scripts/expire_tiles.py
 */

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <string>

#include "expire-tiles.hpp"
#include "options.hpp"
#include "geometry-builder.hpp"
#include "reprojection.hpp"
#include "table.hpp"

#define EARTH_CIRCUMFERENCE		40075016.68
#define HALF_EARTH_CIRCUMFERENCE	(EARTH_CIRCUMFERENCE / 2)
#define TILE_EXPIRY_LEEWAY		0.1		/* How many tiles worth of space to leave either side of a changed feature */
#define EXPIRE_TILES_MAX_BBOX		20000		/* Maximum width or height of a bounding box (metres) */

namespace {
/*
 * We store the dirty tiles in an in-memory tree during runtime
 * and dump them out to a file at the end.  This allows us to easilly drop
 * duplicate tiles from the output.
 *
 * This data structure consists of a node, representing a tile at zoom level 0,
 * which contains 4 pointers to nodes representing each of the child tiles at
 * zoom level 1, and so on down the the zoom level specified in
 * Options->expire_tiles_zoom.
 *
 * The memory allowed to this structure is not capped, but daily deltas
 * generally produce a few hundred thousand expired tiles at zoom level 17,
 * which are easilly accommodated.
 */

int calc_complete(struct expire_tiles::tile * tile) {
	int	c;

	c = tile->complete[0][0];
	c += tile->complete[0][1];
	c += tile->complete[1][0];
	c += tile->complete[1][1];
	return c;
}

void destroy_tree(struct expire_tiles::tile * tree) {
	if (! tree) return;
	if (tree->subtiles[0][0]) destroy_tree(tree->subtiles[0][0]);
	if (tree->subtiles[0][1]) destroy_tree(tree->subtiles[0][1]);
	if (tree->subtiles[1][0]) destroy_tree(tree->subtiles[1][0]);
	if (tree->subtiles[1][1]) destroy_tree(tree->subtiles[1][1]);
	free(tree);
}

/*
 * Mark a tile as dirty.
 * Returns the number of subtiles which have all their children marked as dirty.
 */
int _mark_tile(struct expire_tiles::tile ** tree, int x, int y, int zoom, int this_zoom) {
	int	zoom_diff = zoom - this_zoom - 1;
	int	rel_x;
	int	rel_y;
	int	complete;

	if (! *tree) *tree = (struct expire_tiles::tile *)calloc(1, sizeof(**tree));
	rel_x = (x >> zoom_diff) & 1;
	rel_y = (y >> zoom_diff) & 1;
	if (! (*tree)->complete[rel_x][rel_y]) {
		if (zoom_diff <= 0) {
			(*tree)->complete[rel_x][rel_y] = 1;
		} else {
			complete = _mark_tile(&((*tree)->subtiles[rel_x][rel_y]), x, y, zoom, this_zoom + 1);
			if (complete >= 4) {
				(*tree)->complete[rel_x][rel_y] = 1;
				/* We can destroy the subtree to save memory now all the children are dirty */
				destroy_tree((*tree)->subtiles[rel_x][rel_y]);
				(*tree)->subtiles[rel_x][rel_y] = nullptr;
			}
		}
	}
	return calc_complete(*tree);
}

/*
 * Mark a tile as dirty.
 * Returns the number of subtiles which have all their children marked as dirty.
 */
int mark_tile(struct expire_tiles::tile ** tree_head, int x, int y, int zoom) {
	return _mark_tile(tree_head, x, y, zoom, 0);
}

void output_dirty_tile_impl(FILE * outfile, int x, int y, int zoom, int min_zoom, int &outcount) {
	int	y_min;
	int	x_iter;
	int	y_iter;
	int	x_max;
	int	y_max;
	int	out_zoom;
	int	zoom_diff;

	if (zoom > min_zoom) out_zoom = zoom;
	else out_zoom = min_zoom;
	zoom_diff = out_zoom - zoom;
	y_min = y << zoom_diff;
	x_max = (x + 1) << zoom_diff;
	y_max = (y + 1) << zoom_diff;
	for (x_iter = x << zoom_diff; x_iter < x_max; x_iter++) {
		for (y_iter = y_min; y_iter < y_max; y_iter++) {
			outcount++;
            if ((outcount <= 1) || ((outcount % 1000) == 0)) {
                fprintf(stderr, "\rWriting dirty tile list (%iK)", outcount / 1000);
                fflush(stderr);
            }
            fprintf(outfile, "%i/%i/%i\n", out_zoom, x_iter, y_iter);
		}
	}
}

struct tile_output_file : public expire_tiles::tile_output {
  tile_output_file(const std::string &expire_tiles_filename)
    : outcount(0)
    , outfile(fopen(expire_tiles_filename.c_str(), "a")) {
    if (outfile == nullptr) {
      fprintf(stderr, "Failed to open expired tiles file (%s).  Tile expiry list will not be written!\n", strerror(errno));
    }
  }

  virtual ~tile_output_file() {
    if (outfile) {
      fclose(outfile);
    }
  }

  virtual void output_dirty_tile(int x, int y, int zoom, int min_zoom) {
    output_dirty_tile_impl(outfile, x, y, zoom, min_zoom, outcount);
  }

private:
  int outcount;
  FILE *outfile;
};

void _output_and_destroy_tree(expire_tiles::tile_output *output, struct expire_tiles::tile * tree, int x, int y, int this_zoom, int min_zoom) {
	int	sub_x = x << 1;
	int	sub_y = y << 1;
        expire_tiles::tile_output *out;

	if (! tree) return;

	out = output;
	if ((tree->complete[0][0]) && output) {
		output->output_dirty_tile(sub_x + 0, sub_y + 0, this_zoom + 1, min_zoom);
		out = nullptr;
	}
	if (tree->subtiles[0][0]) _output_and_destroy_tree(out, tree->subtiles[0][0], sub_x + 0, sub_y + 0, this_zoom + 1, min_zoom);

	out = output;
	if ((tree->complete[0][1]) && output) {
		output->output_dirty_tile(sub_x + 0, sub_y + 1, this_zoom + 1, min_zoom);
		out = nullptr;
	}
	if (tree->subtiles[0][1]) _output_and_destroy_tree(out, tree->subtiles[0][1], sub_x + 0, sub_y + 1, this_zoom + 1, min_zoom);

	out = output;
	if ((tree->complete[1][0]) && output) {
		output->output_dirty_tile(sub_x + 1, sub_y + 0, this_zoom + 1, min_zoom);
		out = nullptr;
	}
	if (tree->subtiles[1][0]) _output_and_destroy_tree(out, tree->subtiles[1][0], sub_x + 1, sub_y + 0, this_zoom + 1, min_zoom);

	out = output;
	if ((tree->complete[1][1]) && output) {
		output->output_dirty_tile(sub_x + 1, sub_y + 1, this_zoom + 1, min_zoom);
		out = nullptr;
	}
	if (tree->subtiles[1][1]) _output_and_destroy_tree(out, tree->subtiles[1][1], sub_x + 1, sub_y + 1, this_zoom + 1, min_zoom);

	free(tree);
}

// merge the two trees, destroying b in the process. returns the
// number of completed subtrees.
int _tree_merge(struct expire_tiles::tile **a,
                struct expire_tiles::tile **b) {
  if (*a == nullptr) {
    *a = *b;
    *b = nullptr;

  } else if (*b != nullptr) {
    for (int x = 0; x < 2; ++x) {
      for (int y = 0; y < 2; ++y) {
        // if b is complete on a subtree, then the merged tree must
        // be complete too.
        if ((*b)->complete[x][y]) {
          (*a)->complete[x][y] = (*b)->complete[x][y];
          destroy_tree((*a)->subtiles[x][y]);
          (*a)->subtiles[x][y] = nullptr;

          // but if a is already complete, don't bother moving across
          // anything
        } else if (!(*a)->complete[x][y]) {
          int complete = _tree_merge(&((*a)->subtiles[x][y]), &((*b)->subtiles[x][y]));

          if (complete >= 4) {
            (*a)->complete[x][y] = 1;
            destroy_tree((*a)->subtiles[x][y]);
            (*a)->subtiles[x][y] = nullptr;
          }
        }

        destroy_tree((*b)->subtiles[x][y]);
        (*b)->subtiles[x][y] = nullptr;
      }
    }
  }

  // count the number complete, so we can return it
  int a_complete = 0;
  for (int x = 0; x < 2; ++x) {
    for (int y = 0; y < 2; ++y) {
      if ((*a != nullptr) && ((*a)->complete[x][y])) {
        ++a_complete;
      }
    }
  }

  return a_complete;
}

} // anonymous namespace

void expire_tiles::output_and_destroy(tile_output *output) {
    _output_and_destroy_tree(output, dirty, 0, 0, 0, Options->expire_tiles_zoom_min);
    dirty = nullptr;
}

void expire_tiles::output_and_destroy() {
  if (Options->expire_tiles_zoom >= 0) {
    tile_output_file output(Options->expire_tiles_filename);

    output_and_destroy(&output);
  }
}

expire_tiles::~expire_tiles() {
  if (dirty != nullptr) {
    destroy_tree(dirty);
    dirty = nullptr;
  }
}

expire_tiles::expire_tiles(const struct options_t *options)
    : map_width(0), tile_width(0), Options(options),
      dirty(nullptr)
{
	if (Options->expire_tiles_zoom < 0) return;
	map_width = 1 << Options->expire_tiles_zoom;
	tile_width = EARTH_CIRCUMFERENCE / map_width;
}

void expire_tiles::expire_tile(int x, int y) {
	mark_tile(&dirty, x, y, Options->expire_tiles_zoom);
}

int expire_tiles::normalise_tile_x_coord(int x) {
	x %= map_width;
	if (x < 0) x = (map_width - x) + 1;
	return x;
}

/*
 * Expire tiles that a line crosses
 */
void expire_tiles::from_line(double lon_a, double lat_a, double lon_b, double lat_b) {
	double	tile_x_a;
	double	tile_y_a;
	double	tile_x_b;
	double	tile_y_b;
	double	temp;
	double	x1;
	double	y1;
	double	x2;
	double	y2;
	double	hyp_len;
	double	x_len;
	double	y_len;
	double	x_step;
	double	y_step;
	double	step;
	double	next_step;
	int	x;
	int	y;
	int	norm_x;

    Options->projection->coords_to_tile(&tile_x_a, &tile_y_a, lon_a, lat_a, map_width);
    Options->projection->coords_to_tile(&tile_x_b, &tile_y_b, lon_b, lat_b, map_width);

	if (tile_x_a > tile_x_b) {
		/* We always want the line to go from left to right - swap the ends if it doesn't */
		temp = tile_x_b;
		tile_x_b = tile_x_a;
		tile_x_a = temp;
		temp = tile_y_b;
		tile_y_b = tile_y_a;
		tile_y_a = temp;
	}

	x_len = tile_x_b - tile_x_a;
	if (x_len > map_width / 2) {
		/* If the line is wider than half the map, assume it
           crosses the international date line.
           These coordinates get normalised again later */
		tile_x_a += map_width;
		temp = tile_x_b;
		tile_x_b = tile_x_a;
		tile_x_a = temp;
		temp = tile_y_b;
		tile_y_b = tile_y_a;
		tile_y_a = temp;
	}
	y_len = tile_y_b - tile_y_a;
	hyp_len = sqrt(pow(x_len, 2) + pow(y_len, 2));	/* Pythagoras */
	x_step = x_len / hyp_len;
	y_step = y_len / hyp_len;

	for (step = 0; step <= hyp_len; step+= 0.4) {
		/* Interpolate points 1 tile width apart */
		next_step = step + 0.4;
		if (next_step > hyp_len) next_step = hyp_len;
		x1 = tile_x_a + ((double)step * x_step);
		y1 = tile_y_a + ((double)step * y_step);
		x2 = tile_x_a + ((double)next_step * x_step);
		y2 = tile_y_a + ((double)next_step * y_step);

		/* The line (x1,y1),(x2,y2) is up to 1 tile width long
           x1 will always be <= x2
           We could be smart and figure out the exact tiles intersected,
           but for simplicity, treat the coordinates as a bounding box
           and expire everything within that box. */
		if (y1 > y2) {
			temp = y2;
			y2 = y1;
			y1 = temp;
		}
		for (x = x1 - TILE_EXPIRY_LEEWAY; x <= x2 + TILE_EXPIRY_LEEWAY; x ++) {
			norm_x =  normalise_tile_x_coord(x);
			for (y = y1 - TILE_EXPIRY_LEEWAY; y <= y2 + TILE_EXPIRY_LEEWAY; y ++) {
				expire_tile(norm_x, y);
			}
		}
	}
}

/*
 * Expire tiles within a bounding box
 */
int expire_tiles::from_bbox(double min_lon, double min_lat, double max_lon, double max_lat) {
	double		width;
	double		height;
	int		min_tile_x;
	int		min_tile_y;
	int		max_tile_x;
	int		max_tile_y;
	int		iterator_x;
	int		iterator_y;
	int		norm_x;
	int		ret;
    double  tmp_x;
    double  tmp_y;

	if (Options->expire_tiles_zoom < 0) return 0;

	width = max_lon - min_lon;
	height = max_lat - min_lat;
	if (width > HALF_EARTH_CIRCUMFERENCE + 1) {
		/* Over half the planet's width within the bounding box - assume the
           box crosses the international date line and split it into two boxes */
		ret = from_bbox(-HALF_EARTH_CIRCUMFERENCE, min_lat, min_lon, max_lat);
		ret += from_bbox(max_lon, min_lat, HALF_EARTH_CIRCUMFERENCE, max_lat);
		return ret;
	}

	if (width > EXPIRE_TILES_MAX_BBOX) return -1;
	if (height > EXPIRE_TILES_MAX_BBOX) return -1;


	/* Convert the box's Mercator coordinates into tile coordinates */
        Options->projection->coords_to_tile(&tmp_x, &tmp_y, min_lon, max_lat, map_width);
        min_tile_x = tmp_x - TILE_EXPIRY_LEEWAY;
        min_tile_y = tmp_y - TILE_EXPIRY_LEEWAY;
        Options->projection->coords_to_tile(&tmp_x, &tmp_y, max_lon, min_lat, map_width);
        max_tile_x = tmp_x + TILE_EXPIRY_LEEWAY;
        max_tile_y = tmp_y + TILE_EXPIRY_LEEWAY;
	if (min_tile_x < 0) min_tile_x = 0;
	if (min_tile_y < 0) min_tile_y = 0;
	if (max_tile_x > map_width) max_tile_x = map_width;
	if (max_tile_y > map_width) max_tile_y = map_width;
	for (iterator_x = min_tile_x; iterator_x <= max_tile_x; iterator_x ++) {
		norm_x =  normalise_tile_x_coord(iterator_x);
		for (iterator_y = min_tile_y; iterator_y <= max_tile_y; iterator_y ++) {
			expire_tile(norm_x, iterator_y);
		}
	}
	return 0;
}

void expire_tiles::from_nodes_line(const nodelist_t &nodes)
{
    if (Options->expire_tiles_zoom < 0 || nodes.empty())
        return;

    if (nodes.size() == 1) {
        from_bbox(nodes[0].lon, nodes[0].lat, nodes[0].lon, nodes[0].lat);
    } else {
        for (size_t i = 1; i < nodes.size(); ++i)
            from_line(nodes[i-1].lon, nodes[i-1].lat, nodes[i].lon, nodes[i].lat);
    }
}

/*
 * Calculate a bounding box from a list of nodes and expire all tiles within it
 */
void expire_tiles::from_nodes_poly(const nodelist_t &nodes, osmid_t osm_id)
{
    if (Options->expire_tiles_zoom < 0 || nodes.empty())
        return;

    double min_lon = nodes[0].lon;
    double min_lat = nodes[0].lat;
    double max_lon = nodes[0].lon;
    double max_lat = nodes[0].lat;

    for (size_t i = 1; i < nodes.size(); ++i) {
        if (nodes[i].lon < min_lon) min_lon = nodes[i].lon;
        if (nodes[i].lat < min_lat) min_lat = nodes[i].lat;
        if (nodes[i].lon > max_lon) max_lon = nodes[i].lon;
        if (nodes[i].lat > max_lat) max_lat = nodes[i].lat;
    }

    if (from_bbox(min_lon, min_lat, max_lon, max_lat)) {
        /* Bounding box too big - just expire tiles on the line */
        fprintf(stderr, "\rLarge polygon (%.0f x %.0f metres, OSM ID %" PRIdOSMID ") - only expiring perimeter\n", max_lon - min_lon, max_lat - min_lat, osm_id);
        from_nodes_line(nodes);
    }
}

void expire_tiles::from_xnodes_poly(const multinodelist_t &xnodes, osmid_t osm_id)
{
    for (multinodelist_t::const_iterator it = xnodes.begin(); it != xnodes.end(); ++it)
        from_nodes_poly(*it, osm_id);
}

void expire_tiles::from_xnodes_line(const multinodelist_t &xnodes)
{
    for (multinodelist_t::const_iterator it = xnodes.begin(); it != xnodes.end(); ++it)
        from_nodes_line(*it);
}

void expire_tiles::from_wkb(const char* wkb, osmid_t osm_id)
{
    if (Options->expire_tiles_zoom < 0) return;

    multinodelist_t xnodes;
    bool polygon;

    if (geometry_builder::parse_wkb(wkb, xnodes, &polygon) == 0) {
        if (polygon)
            from_xnodes_poly(xnodes, osm_id);
        else
            from_xnodes_line(xnodes);
    }
}

/*
 * Expire tiles based on an osm element.
 * What type of element (node, line, polygon) osm_id refers to depends on
 * sql_conn. Each type of table has its own sql_conn and the prepared statement
 * get_wkb refers to the appropriate table.
 *
 * The function returns -1 if expiry is not enabled. Otherwise it returns the number
 * of elements that refer to the osm_id.

 */
int expire_tiles::from_db(table_t* table, osmid_t osm_id) {
    //bail if we dont care about expiry
    if (Options->expire_tiles_zoom < 0)
        return -1;

    //grab the geom for this id
    auto wkbs = table->get_wkb_reader(osm_id);

    //dirty the stuff
    const char* wkb = nullptr;
    while((wkb = wkbs.get_next()))
        from_wkb(wkb, osm_id);

    //return how many rows were affected
    return wkbs.get_count();
}

void expire_tiles::merge_and_destroy(expire_tiles &other) {
  if (map_width != other.map_width) {
    throw std::runtime_error((boost::format("Unable to merge tile expiry sets when "
                                            "map_width does not match: %1% != %2%.")
                              % map_width % other.map_width).str());
  }

  if (tile_width != other.tile_width) {
    throw std::runtime_error((boost::format("Unable to merge tile expiry sets when "
                                            "tile_width does not match: %1% != %2%.")
                              % tile_width % other.tile_width).str());
  }

  _tree_merge(&dirty, &other.dirty);

  destroy_tree(other.dirty);
  other.dirty = nullptr;
}
