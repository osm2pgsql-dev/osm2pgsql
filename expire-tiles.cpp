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


struct tile_output_file : public expire_tiles::tile_output
{
  tile_output_file(const char *expire_tiles_filename, int zmin)
  : outcount(0), min_zoom(zmin), outfile(fopen(expire_tiles_filename, "a"))
  {
    if (outfile == nullptr) {
      fprintf(stderr, "Failed to open expired tiles file (%s).  Tile expiry list will not be written!\n", strerror(errno));
    }
  }

  ~tile_output_file() {
    if (outfile) {
      fclose(outfile);
    }
  }

  void output_dirty_tile(int x, int y, int zoom) override
  {
    if (outfile == nullptr) {
        return;
    }

    int out_zoom = std::max(zoom, min_zoom);
    int zoom_diff = out_zoom - zoom;
    int x_max = (x + 1) << zoom_diff;
    int y_max = (y + 1) << zoom_diff;

    for (int x_iter = x << zoom_diff; x_iter < x_max; ++x_iter) {
        for (int y_iter = y << zoom_diff; y_iter < y_max; ++y_iter) {
            ++outcount;
            if ((outcount % 1000) == 0) {
                fprintf(stderr, "\rWriting dirty tile list (%iK)", outcount / 1000);
            }
            fprintf(outfile, "%i/%i/%i\n", out_zoom, x_iter, y_iter);
        }
    }
  }

private:
  int outcount;
  int min_zoom;
  FILE *outfile;
};


int tile::mark_tile(int x, int y, int zoom, int this_zoom)
{
    int zoom_diff = zoom - this_zoom - 1;
    int sub = ((x >> zoom_diff) & 1) << 1 | ((y >> zoom_diff) & 1);

    if (!complete[sub]) {
        if (zoom_diff <= 0) {
            complete[sub] = 1;
            subtiles[sub].reset();
        } else {
            if (!subtiles[sub])
                subtiles[sub].reset(new tile);
            int done = subtiles[sub]->mark_tile(x, y, zoom, this_zoom + 1);
            if (done >= 4) {
                complete[sub] = 1;
                subtiles[sub].reset();
            }
        }
    }

    return num_complete();
}

void tile::output_and_destroy(expire_tiles::tile_output *output,
                        int x, int y, int this_zoom)
{
    int sub_x = x << 1;
    int sub_y = y << 1;

    for (int i = 0; i < 4; ++i) {
        if (complete[i]) {
            output->output_dirty_tile(sub_x + sub2x(i), sub_y + sub2y(i),
                                      this_zoom + 1);
        }
        if (subtiles[i]) {
            subtiles[i]->output_and_destroy(output,
                                            sub_x + sub2x(i), sub_y + sub2y(i),
                                            this_zoom + 1);
            subtiles[i].reset();
        }
    }
}

int tile::merge(tile *other)
{
    for (int i = 0; i < 4; ++i) {
        // if other is complete, then the merge tree must be complete too
        if (other->complete[i]) {
            complete[i] = 1;
            subtiles[i].reset();
        // if our subtree is complete don't bother moving anything
        } else if (!complete[i]) {
            if (subtiles[i]) {
                if (other->subtiles[i]) {
                    int done = subtiles[i]->merge(other->subtiles[i].get());
                    if (done >= 4) {
                        complete[i] = 1;
                        subtiles[i].reset();
                    }
                }
            } else {
                subtiles[i] = std::move(other->subtiles[i]);
            }
        }
        other->subtiles[i].reset();
    }

    return num_complete();
}

void expire_tiles::output_and_destroy(tile_output *output)
{
    if (!dirty)
        return;

    dirty->output_and_destroy(output, 0, 0, 0);
    dirty.reset();
}

void expire_tiles::output_and_destroy(const char *filename, int minzoom)
{
  if (maxzoom >= 0) {
    tile_output_file output(filename, minzoom);

    output_and_destroy(&output);
  }
}

expire_tiles::expire_tiles(int max, double bbox, const std::shared_ptr<reprojection> &proj)
: max_bbox(bbox), maxzoom(max), projection(proj)
{
    if (maxzoom >= 0) {
        map_width = 1 << maxzoom;
        tile_width = EARTH_CIRCUMFERENCE / map_width;
    }
}

void expire_tiles::expire_tile(int x, int y)
{
    if (!dirty)
        dirty.reset(new tile);

    dirty->mark_tile(x, y, maxzoom, 0);
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

    projection->coords_to_tile(&tile_x_a, &tile_y_a, lon_a, lat_a, map_width);
    projection->coords_to_tile(&tile_x_b, &tile_y_b, lon_b, lat_b, map_width);

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

	if (maxzoom < 0) return 0;

	width = max_lon - min_lon;
	height = max_lat - min_lat;
	if (width > HALF_EARTH_CIRCUMFERENCE + 1) {
		/* Over half the planet's width within the bounding box - assume the
           box crosses the international date line and split it into two boxes */
		ret = from_bbox(-HALF_EARTH_CIRCUMFERENCE, min_lat, min_lon, max_lat);
		ret += from_bbox(max_lon, min_lat, HALF_EARTH_CIRCUMFERENCE, max_lat);
		return ret;
	}

    if (width > max_bbox || height > max_bbox) {
        return -1;
    }


	/* Convert the box's Mercator coordinates into tile coordinates */
        projection->coords_to_tile(&tmp_x, &tmp_y, min_lon, max_lat, map_width);
        min_tile_x = tmp_x - TILE_EXPIRY_LEEWAY;
        min_tile_y = tmp_y - TILE_EXPIRY_LEEWAY;
        projection->coords_to_tile(&tmp_x, &tmp_y, max_lon, min_lat, map_width);
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
    if (maxzoom < 0 || nodes.empty())
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
    if (maxzoom < 0 || nodes.empty())
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
    if (maxzoom < 0) return;

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
    if (maxzoom < 0)
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

void expire_tiles::merge_and_destroy(expire_tiles &other)
{
  if (!other.dirty) {
      return;
  }

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


  if (!dirty) {
      dirty = std::move(other.dirty);
  } else {
      dirty->merge(other.dirty.get());
  }

  other.dirty.reset();
}
