/* reprojection.h
 *
 * Convert OSM lattitude / longitude from degrees to mercator
 * so that Mapnik does not have to project the data again
 *
 */

#ifndef REPROJECTION_H
#define REPROJECTION_H
void project_init(void);
void project_exit(void);
void reproject(double *lat, double *lon);
#endif
