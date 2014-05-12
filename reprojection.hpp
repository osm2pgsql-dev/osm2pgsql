/* reprojection.h
 *
 * Convert OSM lattitude / longitude from degrees to mercator
 * so that Mapnik does not have to project the data again
 *
 */

#ifndef REPROJECTION_H
#define REPROJECTION_H

struct Projection_Info {
    Projection_Info(const char *descr_, const char *proj4text_, int srs_, const char *option_);

    const char *descr;
    const char *proj4text;
    const int srs;
    const char *option;
};

enum Projection { PROJ_LATLONG = 0, PROJ_MERC, PROJ_SPHERE_MERC,   PROJ_COUNT };
void project_init(int);
void project_exit(void);
struct Projection_Info const* project_getprojinfo(void);
void reproject(double *lat, double *lon);
void coords_to_tile(double *tilex, double *tiley, double lon, double lat);

extern const struct Projection_Info Projection_Infos[];

#endif
