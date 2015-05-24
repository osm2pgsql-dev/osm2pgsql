/* reprojection.h
 *
 * Convert OSM lattitude / longitude from degrees to mercator
 * so that Mapnik does not have to project the data again
 *
 */

#ifndef REPROJECTION_H
#define REPROJECTION_H

#include <boost/noncopyable.hpp>

struct Projection_Info {
    Projection_Info(const char *descr_, const char *proj4text_, int srs_, const char *option_);

    const char *descr;
    const char *proj4text;
    const int srs;
    const char *option;
};

enum Projection { PROJ_LATLONG = 0, PROJ_SPHERE_MERC, PROJ_COUNT };

struct reprojection : public boost::noncopyable
{
    explicit reprojection(int proj);
    ~reprojection();

    struct Projection_Info const* project_getprojinfo(void);
    void reproject(double *lat, double *lon);
    void coords_to_tile(double *tilex, double *tiley, double lon, double lat, int map_width);
    int get_proj_id() const;

private:
    int Proj;

    /** The projection of the source data. Always lat/lon (EPSG:4326). */
    void *pj_source;

    /** The target projection (used in the PostGIS tables). Controlled by the -l/-M/-m/-E options. */
    void *pj_target;

    /** The projection used for tiles. Currently this is fixed to be Spherical
     *  Mercator. You will usually have tiles in the same projection as used
     *  for PostGIS, but it is theoretically possible to have your PostGIS data
     *  in, say, lat/lon but still create tiles in Spherical Mercator.
     */
    void *pj_tile;

    struct Projection_Info *custom_projection;
};

#endif
