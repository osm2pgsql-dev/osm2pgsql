/* reprojection.c
 *
 * Convert OSM coordinates to another coordinate system for
 * the database (usually convert lat/lon to Spherical Mercator
 * so Mapnik doesn't have to).
 */

#include "config.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <osmium/geom/mercator_projection.hpp>

#include "reprojection.hpp"

/** must match expire.tiles.c */
#define EARTH_CIRCUMFERENCE              40075016.68

namespace {

void latlon2merc(double *lat, double *lon)
{
    if (*lat > 89.99) {
        *lat = 89.99;
    } else if (*lat < -89.99) {
        *lat = -89.99;
    }

    using namespace osmium::geom;
    auto coord = lonlat_to_mercator(Coordinates(*lon, *lat));
    *lon = coord.x;
    *lat = coord.y;
}

class latlon_reprojection_t : public reprojection
{
public:
    osmium::geom::Coordinates reproject(osmium::Location loc) const override
    {
        return osmium::geom::Coordinates(loc.lon_without_check(),
                                         loc.lat_without_check());
    }

    void target_to_tile(double *lat, double *lon) const override
    {
        latlon2merc(lat, lon);
    }

    int target_srs() const override { return PROJ_LATLONG; }
    const char *target_desc() const override { return "Latlong"; }
};

class merc_reprojection_t : public reprojection
{
public:
    osmium::geom::Coordinates reproject(osmium::Location loc) const override
    {
        double lat = loc.lat_without_check();
        double lon = loc.lon_without_check();

        latlon2merc(&lat, &lon);

        return osmium::geom::Coordinates(lon, lat);
    }

    void target_to_tile(double *, double *) const override
    { /* nothing */ }

    int target_srs() const override { return PROJ_SPHERE_MERC; }
    const char *target_desc() const override { return "Spherical Mercator"; }
};

class generic_reprojection_t : public reprojection
{
public:
    generic_reprojection_t(int srs)
    : m_target_srs(srs), pj_target(srs), pj_source(PROJ_LATLONG),
      pj_tile("+proj=merc +a=6378137 +b=6378137 +lat_ts=0.0 +lon_0=0.0 +x_0=0.0 +y_0=0 +k=1.0 +units=m +nadgrids=@null +wktext  +no_defs")
    {}

    osmium::geom::Coordinates reproject(osmium::Location loc) const override
    {
        using namespace osmium::geom;
        return transform(pj_source, pj_target,
                         Coordinates(deg_to_rad(loc.lon_without_check()),
                                     deg_to_rad(loc.lat_without_check())));
    }

    void target_to_tile(double *lat, double *lon) const override
    {
        auto c = transform(pj_target, pj_tile, osmium::geom::Coordinates(*lon, *lat));

        *lon = c.x;
        *lat = c.y;
    }

    int target_srs() const override { return m_target_srs; }
    const char *target_desc() const override { return pj_get_def(pj_target.get(), 0); }

private:
    int m_target_srs;
    osmium::geom::CRS pj_target;
    /** The projection of the source data. Always lat/lon (EPSG:4326). */
    osmium::geom::CRS pj_source;

    /** The projection used for tiles. Currently this is fixed to be Spherical
     *  Mercator. You will usually have tiles in the same projection as used
     *  for PostGIS, but it is theoretically possible to have your PostGIS data
     *  in, say, lat/lon but still create tiles in Spherical Mercator.
     */
    osmium::geom::CRS pj_tile;

};

} // anonymous namespace


reprojection *reprojection::create_projection(int srs)
{
    switch (srs)
    {
        case PROJ_LATLONG: return new latlon_reprojection_t();
        case PROJ_SPHERE_MERC: return new merc_reprojection_t();
    }

    return new generic_reprojection_t(srs);
}


void reprojection::coords_to_tile(double *tilex, double *tiley,
                                  double lon, double lat, int map_width)
{
    target_to_tile(&lat, &lon);

    *tilex = map_width * (0.5 + lon / EARTH_CIRCUMFERENCE);
    *tiley = map_width * (0.5 - lat / EARTH_CIRCUMFERENCE);
}
