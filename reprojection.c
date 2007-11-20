/* reprojection.c
 *
 * Convert OSM lattitude / longitude from degrees to mercator
 * so that Mapnik does not have to project the data again
 *
 */

#include <stdio.h>
#include <unistd.h>
#include <proj_api.h>

#include "reprojection.h"

static projPJ pj_ll, pj_merc;
static enum Projection Proj;
const struct Projection_Info Projection_Info[] = {
  [PROJ_LATLONG] = { 
     descr: "Latlong", 
     proj4text: "(none)", 
     srs:4326, 
     option: "-l" },
  [PROJ_MERC]    = { 
     descr: "OSM (false) Mercator", 
     proj4text: "+proj=merc +datum=WGS84  +k=1.0 +units=m +over +no_defs", 
     srs:3395, 
     option: "" },
  [PROJ_SPHERE_MERC] = { 
     descr: "Spherical Mercator",  
     proj4text: "+proj=merc +a=6378137 +b=6378137 +lat_ts=0.0 +lon_0=0.0 +x_0=0.0 +y_0=0 +k=1.0 +units=m +nadgrids=@null +no_defs +over", 
     srs:900913, 
     option: "-m" }
};

void project_init(enum Projection proj)
{
	Proj = proj;
	
	if( proj == PROJ_LATLONG )
		return;
		
	pj_ll   = pj_init_plus("+proj=latlong +ellps=GRS80 +no_defs");
	pj_merc = pj_init_plus( Projection_Info[proj].proj4text );
			
	if (!pj_ll || !pj_merc) {
		fprintf(stderr, "Projection code failed to initialise\n");
		exit(1);
	}
}

void project_exit(void)
{
	if( Proj == PROJ_LATLONG )
		return;
		
	pj_free(pj_ll);
	pj_ll = NULL;
	pj_free(pj_merc);
	pj_merc = NULL;
}

struct Projection_Info const *project_getprojinfo(void)
{
  return &Projection_Info[Proj];
}

void reproject(double *lat, double *lon)
{
	double x[1], y[1], z[1];
	
	if( Proj == PROJ_LATLONG )
		return;

	x[0] = *lon * DEG_TO_RAD;
	y[0] = *lat * DEG_TO_RAD;
	z[0] = 0;
	
	pj_transform(pj_ll, pj_merc, 1, 1, x, y, z);
	
	//printf("%.4f\t%.4f -> %.4f\t%.4f\n", *lat, *lon, y[0], x[0]);	
	*lat = y[0];
	*lon = x[0];
}

