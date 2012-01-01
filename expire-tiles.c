/*
 * Dirty tile list generation
 *
 * Steve Hill <steve@nexusuk.org>
 *
 * Please refer to the OpenPisteMap expire_tiles.py script for a demonstration
 * of how to make use of the output:
 * https://subversion.nexusuk.org/trac/browser/openpistemap/trunk/scripts/expire_tiles.py
 */

#include <libpq-fe.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "expire-tiles.h"
#include "output.h"
#include "pgsql.h"
#include "build_geometry.h"
#include "reprojection.h"

#define EARTH_CIRCUMFERENCE		40075016.68
#define HALF_EARTH_CIRCUMFERENCE	(EARTH_CIRCUMFERENCE / 2)
#define TILE_EXPIRY_LEEWAY		0.1		// How many tiles worth of space to leave either side of a changed feature
#define EXPIRE_TILES_MAX_BBOX		20000		// Maximum width or height of a bounding box (metres)

struct tile {
	int		complete[2][2];	// Flags
	struct tile *	subtiles[2][2];
};

int map_width; /* not "static" since used in reprojection.c! */
static double				tile_width;
static const struct output_options *	Options;
static struct tile *			dirty = NULL;
static int				outcount;

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

static int calc_complete(struct tile * tile) {
	int	c;

	c = tile->complete[0][0];
	c += tile->complete[0][1];
	c += tile->complete[1][0];
	c += tile->complete[1][1];
	return c;
}

static void destroy_tree(struct tile * tree) {
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
static int _mark_tile(struct tile ** tree, int x, int y, int zoom, int this_zoom) {
	int	zoom_diff = zoom - this_zoom;
	int	rel_x;
	int	rel_y;
	int	complete;

	if (! *tree) *tree = calloc(1, sizeof(**tree));
	zoom_diff = (zoom - this_zoom) - 1;
	rel_x = (x >> zoom_diff) & 1;
	rel_y = (y >> zoom_diff) & 1;
	if (! (*tree)->complete[rel_x][rel_y]) {
		if (zoom_diff <= 0) {
			(*tree)->complete[rel_x][rel_y] = 1;
		} else {
			complete = _mark_tile(&((*tree)->subtiles[rel_x][rel_y]), x, y, zoom, this_zoom + 1);
			if (complete >= 4) {
				(*tree)->complete[rel_x][rel_y] = 1;
				// We can destroy the subtree to save memory now all the children are dirty
				destroy_tree((*tree)->subtiles[rel_x][rel_y]);
				(*tree)->subtiles[rel_x][rel_y] = NULL;
			}
		}
	}
	return calc_complete(*tree);
}

/*
 * Mark a tile as dirty.
 * Returns the number of subtiles which have all their children marked as dirty.
 */
static int mark_tile(struct tile ** tree_head, int x, int y, int zoom) {
	return _mark_tile(tree_head, x, y, zoom, 0);
}

static void output_dirty_tile(FILE * outfile, int x, int y, int zoom, int min_zoom) {
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
			if ((outcount <= 1) || (! (outcount % 1000))) {
				fprintf(stderr, "\rWriting dirty tile list (%iK)", outcount / 1000);
				fflush(stderr);
			}
			fprintf(outfile, "%i/%i/%i\n", out_zoom, x_iter, y_iter);
		}
	}
}

static void _output_and_destroy_tree(FILE * outfile, struct tile * tree, int x, int y, int this_zoom, int min_zoom) {
	int	sub_x = x << 1;
	int	sub_y = y << 1;
	FILE *	ofile;

	if (! tree) return;

	ofile = outfile;
	if ((tree->complete[0][0]) && outfile) {
		output_dirty_tile(outfile, sub_x + 0, sub_y + 0, this_zoom + 1, min_zoom);
		ofile = NULL;
	}
	if (tree->subtiles[0][0]) _output_and_destroy_tree(ofile, tree->subtiles[0][0], sub_x + 0, sub_y + 0, this_zoom + 1, min_zoom);

	ofile = outfile;
	if ((tree->complete[0][1]) && outfile) {
		output_dirty_tile(outfile, sub_x + 0, sub_y + 1, this_zoom + 1, min_zoom);
		ofile = NULL;
	}
	if (tree->subtiles[0][1]) _output_and_destroy_tree(ofile, tree->subtiles[0][1], sub_x + 0, sub_y + 1, this_zoom + 1, min_zoom);

	ofile = outfile;
	if ((tree->complete[1][0]) && outfile) {
		output_dirty_tile(outfile, sub_x + 1, sub_y + 0, this_zoom + 1, min_zoom);
		ofile = NULL;
	}
	if (tree->subtiles[1][0]) _output_and_destroy_tree(ofile, tree->subtiles[1][0], sub_x + 1, sub_y + 0, this_zoom + 1, min_zoom);

	ofile = outfile;
	if ((tree->complete[1][1]) && outfile) {
		output_dirty_tile(outfile, sub_x + 1, sub_y + 1, this_zoom + 1, min_zoom);
		ofile = NULL;
	}
	if (tree->subtiles[1][1]) _output_and_destroy_tree(ofile, tree->subtiles[1][1], sub_x + 1, sub_y + 1, this_zoom + 1, min_zoom);

	free(tree);
}

static void output_and_destroy_tree(FILE * outfile, struct tile * tree) {
	_output_and_destroy_tree(outfile, tree, 0, 0, 0, Options->expire_tiles_zoom_min);
}

void expire_tiles_stop(void) {
	FILE *	outfile;

	if (Options->expire_tiles_zoom < 0) return;
	outcount = 0;
	if ((outfile = fopen(Options->expire_tiles_filename, "a"))) {
	    output_and_destroy_tree(outfile, dirty);
	    fclose(outfile);
	} else {
        fprintf(stderr, "Failed to open expired tiles file (%s).  Tile expiry list will not be written!\n", strerror(errno));
    }
	dirty = NULL;
}

void expire_tiles_init(const struct output_options *options) {
	Options = options;
	if (Options->expire_tiles_zoom < 0) return;
	map_width = 1 << Options->expire_tiles_zoom;
	tile_width = EARTH_CIRCUMFERENCE / map_width;
}

static void expire_tile(int x, int y) {
	mark_tile(&dirty, x, y, Options->expire_tiles_zoom);
}

static int normalise_tile_x_coord(int x) {
	x %= map_width;
	if (x < 0) x = (map_width - x) + 1;
	return x;
}

/*
 * Expire tiles that a line crosses
 */
static void expire_tiles_from_line(double lon_a, double lat_a, double lon_b, double lat_b) {
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

    coords_to_tile(&tile_x_a, &tile_y_a, lon_a, lat_a);
    coords_to_tile(&tile_x_b, &tile_y_b, lon_b, lat_b);

	if (tile_x_a > tile_x_b) {
		// We always want the line to go from left to right - swap the ends if it doesn't
		temp = tile_x_b;
		tile_x_b = tile_x_a;
		tile_x_a = temp;
		temp = tile_y_b;
		tile_y_b = tile_y_a;
		tile_y_a = temp;
	}

	x_len = tile_x_b - tile_x_a;
	if (x_len > map_width / 2) {
		// If the line is wider than half the map, assume it
		// crosses the international date line.
		// These coordinates get normalised again later
		tile_x_a += map_width;
		temp = tile_x_b;
		tile_x_b = tile_x_a;
		tile_x_a = temp;
		temp = tile_y_b;
		tile_y_b = tile_y_a;
		tile_y_a = temp;
	}
	y_len = tile_y_b - tile_y_a;
	hyp_len = sqrt(pow(x_len, 2) + pow(y_len, 2));	// Pythagoras
	x_step = x_len / hyp_len;
	y_step = y_len / hyp_len;
//	fprintf(stderr, "Expire from line (%f,%f),(%f,%f) [%f,%f],[%f,%f] %fx%f hyp_len = %f\n", lon_a, lat_a, lon_b, lat_b, tile_x_a, tile_y_a, tile_x_b, tile_y_b, x_len, y_len, hyp_len);
	
	for (step = 0; step <= hyp_len; step+= 0.4) {
		// Interpolate points 1 tile width apart
		next_step = step + 0.4;
		if (next_step > hyp_len) next_step = hyp_len;
		x1 = tile_x_a + ((double)step * x_step);
		y1 = tile_y_a + ((double)step * y_step);
		x2 = tile_x_a + ((double)next_step * x_step);
		y2 = tile_y_a + ((double)next_step * y_step);
		
//		printf("Expire from subline (%f,%f),(%f,%f)\n", x1, y1, x2, y2);
		// The line (x1,y1),(x2,y2) is up to 1 tile width long
		// x1 will always be <= x2
		// We could be smart and figure out the exact tiles intersected,
		// but for simplicity, treat the coordinates as a bounding box
		// and expire everything within that box.
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
int expire_tiles_from_bbox(double min_lon, double min_lat, double max_lon, double max_lat) {
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
		// Over half the planet's width within the bounding box - assume the
		// box crosses the international date line and split it into two boxes
		ret = expire_tiles_from_bbox(-HALF_EARTH_CIRCUMFERENCE, min_lat, min_lon, max_lat);
		ret += expire_tiles_from_bbox(max_lon, min_lat, HALF_EARTH_CIRCUMFERENCE, max_lat);
		return ret;
	}

	if (width > EXPIRE_TILES_MAX_BBOX) return -1;
	if (height > EXPIRE_TILES_MAX_BBOX) return -1;

//	printf("Expire from bbox (%f,%f)-(%f,%f) %fx%f\n", min_lon, min_lat, min_lon, min_lat, width, height);

	// Convert the box's Mercator coordinates into tile coordinates
    coords_to_tile(&tmp_x, &tmp_y, min_lon, max_lat);
    min_tile_x = tmp_x - TILE_EXPIRY_LEEWAY;
    min_tile_y = tmp_y - TILE_EXPIRY_LEEWAY;
    coords_to_tile(&tmp_x, &tmp_y, max_lon, min_lat);
    max_tile_x = tmp_x + TILE_EXPIRY_LEEWAY;
    max_tile_y = tmp_y + TILE_EXPIRY_LEEWAY;
	if (min_tile_x < 0) min_tile_x = 0;
	if (min_tile_y < 0) min_tile_y = 0;
	if (max_tile_x > map_width) max_tile_x = map_width;
	if (max_tile_y > map_width) max_tile_y = map_width;
//	printf("BBOX: (%f %f) - (%f %f) [%i %i] - [%i %i]\n", min_lon, min_lat, max_lon, max_lat, min_tile_x, min_tile_y, max_tile_x, max_tile_y);
	for (iterator_x = min_tile_x; iterator_x <= max_tile_x; iterator_x ++) {
		norm_x =  normalise_tile_x_coord(iterator_x);
		for (iterator_y = min_tile_y; iterator_y <= max_tile_y; iterator_y ++) {
			expire_tile(norm_x, iterator_y);
		}
	}
	return 0;
}

void expire_tiles_from_nodes_line(struct osmNode * nodes, int count) {
	int	i;
	double	last_lat;
	double	last_lon;

	if (Options->expire_tiles_zoom < 0) return;
//	fprintf(stderr, "Expire from nodes_line (%i)\n", count);
	if (count < 1) return;
	last_lat = nodes[0].lat;
	last_lon = nodes[0].lon;
	if (count < 2) {
		expire_tiles_from_bbox(last_lon, last_lat, last_lon, last_lat);
		return;
	}
	for (i = 1; i < count; i ++) {
		expire_tiles_from_line(last_lon, last_lat, nodes[i].lon, nodes[i].lat);
		last_lat = nodes[i].lat;
		last_lon = nodes[i].lon;
	}
}

/*
 * Calculate a bounding box from a list of nodes and expire all tiles within it
 */
void expire_tiles_from_nodes_poly(struct osmNode * nodes, int count, osmid_t osm_id) {
	int	i;
	int	got_coords = 0;
	double	min_lon = 0.0;
	double	min_lat = 0.0;
	double	max_lon = 0.0;
	double	max_lat = 0.0;
        
	if (Options->expire_tiles_zoom < 0) return;
//	printf("Expire from nodes_poly (%i)\n", count);
	for (i = 0; i < count; i++) {
		if ((! got_coords) || (nodes[i].lon < min_lon)) min_lon = nodes[i].lon;
		if ((! got_coords) || (nodes[i].lat < min_lat)) min_lat = nodes[i].lat;
		if ((! got_coords) || (nodes[i].lon > max_lon)) max_lon = nodes[i].lon;
		if ((! got_coords) || (nodes[i].lat > max_lat)) max_lat = nodes[i].lat;
		got_coords = 1;
	}
	if (got_coords) {
		if (expire_tiles_from_bbox(min_lon, min_lat, max_lon, max_lat)) {
			// Bounding box too big - just expire tiles on the line
			fprintf(stderr, "\rLarge polygon (%.0f x %.0f metres, OSM ID %" PRIdOSMID ") - only expiring perimeter\n", max_lon - min_lon, max_lat - min_lat, osm_id);
			expire_tiles_from_nodes_line(nodes, count);
		}
	}
}

static void expire_tiles_from_xnodes_poly(struct osmNode ** xnodes, int * xcount, osmid_t osm_id) {
	int	i;

        for (i = 0; xnodes[i]; i++) expire_tiles_from_nodes_poly(xnodes[i], xcount[i], osm_id);
}

static void expire_tiles_from_xnodes_line(struct osmNode ** xnodes, int * xcount) {
	int	i;

        for (i = 0; xnodes[i]; i++) expire_tiles_from_nodes_line(xnodes[i], xcount[i]);
}

void expire_tiles_from_wkt(const char * wkt, osmid_t osm_id) {
	struct osmNode **	xnodes;
	int *			xcount;
	int			polygon;
	int			i;

	if (Options->expire_tiles_zoom < 0) return;
	if (! parse_wkt(wkt, &xnodes, &xcount, &polygon)) {
		if (polygon) expire_tiles_from_xnodes_poly(xnodes, xcount, osm_id);
		else expire_tiles_from_xnodes_line(xnodes, xcount);
		for (i = 0; xnodes[i]; i++) free(xnodes[i]);
		free(xnodes);
		free(xcount);
	}
}

/*
 * Expire tiles based on an osm element.
 * What type of element (node, line, polygon) osm_id refers to depends on
 * sql_conn. Each type of table has its own sql_conn and the prepared statement
 * get_wkt refers to the appropriate table.
 *
 * The function returns -1 if expiry is not enabled. Otherwise it returns the number
 * of elements that refer to the osm_id.

 */
int expire_tiles_from_db(PGconn * sql_conn, osmid_t osm_id) {
    PGresult *	res;
    char *		wkt;
    int i, noElements = 0;
    char const *paramValues[1];
    char tmp[16];

    if (Options->expire_tiles_zoom < 0) return -1;
    snprintf(tmp, sizeof(tmp), "%" PRIdOSMID, osm_id);
    paramValues[0] = tmp;
    
    /* The prepared statement get_wkt will behave differently depending on the sql_conn
     * each table has its own sql_connection with the get_way refering to the approriate table
     */
    res = pgsql_execPrepared(sql_conn, "get_wkt", 1, (const char * const *)paramValues, PGRES_TUPLES_OK);
    noElements = PQntuples(res);

    for (i = 0; i < noElements; i++) {
        wkt = PQgetvalue(res, i, 0);
        expire_tiles_from_wkt(wkt, osm_id);
    }
    PQclear(res);
    return noElements;
}


