/* reprojection.c
 *
 * Convert OSM lattitude / longitude from degrees to mercator
 * so that Mapnik does not have to project the data again
 *
 */

#include <stdio.h>
#include <unistd.h>
#include <proj_api.h>

static projPJ pj_ll, pj_merc;

void project_init(void)
{
	pj_ll   = pj_init_plus("+proj=latlong +ellps=GRS80 +no_defs");
	pj_merc = pj_init_plus("+proj=merc +datum=WGS84  +k=1.0 +units=m +over +no_defs");
			
	if (!pj_ll || !pj_merc) {
		fprintf(stderr, "Projection code failed to initialise\n");
		exit(1);
	}
}

void project_exit(void)
{
	pj_free(pj_ll);
	pj_ll = NULL;
	pj_free(pj_merc);
	pj_merc = NULL;
}

void reproject(double *lat, double *lon)
{
	double x[1], y[1], z[1];
	//projUV p, q;
	//p.u = *lat * DEG_TO_RAD;
	//p.v = *lon * DEG_TO_RAD;

	x[0] = *lon * DEG_TO_RAD;
	y[0] = *lat * DEG_TO_RAD;
	z[0] = 0;
	
	pj_transform(pj_ll, pj_merc, 1, 1, x, y, z);
	
	//printf("%.4f\t%.4f -> %.4f\t%.4f\n", *lat, *lon, y[0], x[0]);	
	*lat = y[0];
	*lon = x[0];
}

