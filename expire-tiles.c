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
#include "expire-tiles.h"
#include "output.h"
#include "pgsql.h"
#include "build_geometry.h"

#define EARTH_CIRCUMFERENCE		40075016.68
#define HALF_EARTH_CIRCUMFERENCE	(EARTH_CIRCUMFERENCE / 2)
#define TILE_EXPIRY_LEEWAY		0.5		// How many tiles worth of space to leave either side of a changed feature
#define EXPIRE_TILES_MAX_BBOX		30000		// Maximum width or height of a bounding box (metres)

struct tile_subtree {
	struct tile_subtree *	less;
	struct tile_subtree *	greater;
	int			y;
};

struct tile_tree {
	struct tile_tree *	less;
	struct tile_tree *	greater;
	int			x;
	struct tile_subtree *	subtree;
};

static int				map_width;
static double				tile_width;
static const struct output_options *	Options;
static struct tile_tree *		dirty = NULL;
static int				outcount;

/*
 * We store the dirty tiles in an in-memory binary tree during runtime
 * and dump them out to a file at the end.  This allows us to easilly drop
 * duplicate tiles from the output.
 *
 * This consists of a binary tree of the X coordinate of each dirty tile, with
 * each node containing a binary tree of the Y coordinate.
 *
 * The memory allowed to this structure is not capped, but daily deltas
 * generally produce a few hundred thousand expired tiles at zoom level 17,
 * which are easilly accommodated.
 */
static void add_to_subtree(struct tile_subtree ** tree, int y) {
	while (*tree) {
		if (y < (*tree)->y) tree = &((*tree)->less);
		else if (y > (*tree)->y) tree = &((*tree)->greater);
		else return;	// Already in the tree
	}
	*tree = calloc(1, sizeof(**tree));
	(*tree)->y = y;
}

static void add_to_tree(struct tile_tree ** tree, int x, int y) {
	while (*tree) {
		if (x < (*tree)->x) tree = &((*tree)->less);
		else if (x > (*tree)->x) tree = &((*tree)->greater);
		else {
			add_to_subtree(&((*tree)->subtree), y);
			return;
		}
	}
	*tree = calloc(1, sizeof(**tree));
	(*tree)->x = x;
	add_to_subtree(&((*tree)->subtree), y);
}

static void output_and_destroy_subtree(FILE * outfile, struct tile_subtree ** tree, int x) {
	if (! *tree) return;
	output_and_destroy_subtree(outfile, &((*tree)->less), x);
	if (outfile) {
		outcount++;
		if ((outcount <= 1) || (! (outcount % 1000))) {
			fprintf(stderr, "\rWriting dirty tile list (%iK)", outcount / 1000);
			fflush(stderr);
		}
		fprintf(outfile, "%i/%i/%i\n", Options->expire_tiles_zoom, x, (*tree)->y);
	}
	output_and_destroy_subtree(outfile, &((*tree)->greater), x);
	free(*tree);
}

static void output_and_destroy_tree(FILE * outfile, struct tile_tree ** tree) {
	if (! *tree) return;
	output_and_destroy_tree(outfile, &((*tree)->less));
	output_and_destroy_subtree(outfile, &((*tree)->subtree), (*tree)->x);
	output_and_destroy_tree(outfile, &((*tree)->greater));
	free(*tree);
}

void expire_tiles_stop(void) {
	FILE *	outfile;

	if (Options->expire_tiles_zoom < 0) return;
	outcount = 0;
	outfile = fopen(Options->expire_tiles_filename, "a");
	output_and_destroy_tree(outfile, &dirty);
	if (outfile) fclose(outfile);
	else fprintf(stderr, "Failed to open expired tiles file.  Tile expiry list will now be written!\n");
}

void expire_tiles_init(const struct output_options *options) {
	Options = options;
	if (Options->expire_tiles_zoom < 0) return;
	map_width = pow(2,Options->expire_tiles_zoom);
	tile_width = EARTH_CIRCUMFERENCE / map_width;
}

static double coords_to_tile_x(double lon) {
	return map_width * (0.5 + (lon / EARTH_CIRCUMFERENCE));
}

static double coords_to_tile_y(double lat) {
	return map_width * (0.5 - (lat / EARTH_CIRCUMFERENCE));
}

static void expire_tile(int x, int y) {
	add_to_tree(&dirty, x, y);
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

	tile_x_a = coords_to_tile_x(lon_a);
	tile_y_a = coords_to_tile_y(lat_a);
	tile_x_b = coords_to_tile_x(lon_b);
	tile_y_b = coords_to_tile_y(lat_b);
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
	
	for (step = 0; step <= hyp_len; step ++) {
		// Interpolate points 1 tile width apart
		next_step = step + 1;
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
	min_tile_x = coords_to_tile_x(min_lon) - TILE_EXPIRY_LEEWAY;
	max_tile_y = coords_to_tile_x(min_lat) + TILE_EXPIRY_LEEWAY;
	max_tile_x = coords_to_tile_x(min_lon) + TILE_EXPIRY_LEEWAY;
	min_tile_y = coords_to_tile_x(min_lat) - TILE_EXPIRY_LEEWAY;
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
void expire_tiles_from_nodes_poly(struct osmNode * nodes, int count, int osm_id) {
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
			fprintf(stderr, "\rLarge polygon (%.0f x %.0f metres, OSM ID %i) - only expiring perimeter\n", max_lon - min_lon, max_lat - min_lat, osm_id);
			expire_tiles_from_nodes_line(nodes, count);
		}
	}
}

static void expire_tiles_from_xnodes_poly(struct osmNode ** xnodes, int * xcount, int osm_id) {
	int	i;

        for (i = 0; xnodes[i]; i++) expire_tiles_from_nodes_poly(xnodes[i], xcount[i], osm_id);
}

static void expire_tiles_from_xnodes_line(struct osmNode ** xnodes, int * xcount) {
	int	i;

        for (i = 0; xnodes[i]; i++) expire_tiles_from_nodes_line(xnodes[i], xcount[i]);
}

void expire_tiles_from_wkt(const char * wkt, int osm_id) {
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

void expire_tiles_from_db(PGconn * sql_conn, int osm_id) {
	PGresult *	res;
	char		tmp[16];
	char const *	paramValues[1];
	int		tuple;
	char *		wkt;

	if (Options->expire_tiles_zoom < 0) return;
	snprintf(tmp, sizeof(tmp), "%d", osm_id);
	paramValues[0] = tmp;
	res = pgsql_execPrepared(sql_conn, "get_way", 1, paramValues, PGRES_TUPLES_OK);
	for (tuple = 0; tuple < PQntuples(res); tuple++) {
		wkt = PQgetvalue(res, tuple, 0);
		expire_tiles_from_wkt(wkt, osm_id);
	}
	PQclear(res);
}


