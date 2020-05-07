/* reprojection.c
 *
 * Convert OSM coordinates to another coordinate system for
 * the database (usually convert lat/lon to Spherical Mercator
 * so Mapnik doesn't have to).
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <osmium/geom/mercator_projection.hpp>

#include "reprojection.hpp"

/** must match expire.tiles.c */
#define EARTH_CIRCUMFERENCE 40075016.68

namespace {

void latlon2merc(double *lat, double *lon)
{
    if (*lat > 89.99) {
        *lat = 89.99;
    } else if (*lat < -89.99) {
        *lat = -89.99;
    }

    using namespace osmium::geom;
    auto const coord = lonlat_to_mercator(Coordinates{*lon, *lat});
    *lon = coord.x;
    *lat = coord.y;
}

class latlon_reprojection_t : public reprojection
{
public:
    osmium::geom::Coordinates reproject(osmium::Location loc) const override
    {
        return osmium::geom::Coordinates{loc.lon_without_check(),
                                         loc.lat_without_check()};
    }

    void target_to_tile(double *lat, double *lon) const override
    {
        latlon2merc(lat, lon);
    }

    int target_srs() const override { return PROJ_LATLONG; }
    char const *target_desc() const override { return "Latlong"; }
};

class merc_reprojection_t : public reprojection
{
public:
    osmium::geom::Coordinates reproject(osmium::Location loc) const override
    {
        double lat = loc.lat_without_check();
        double lon = loc.lon_without_check();

        latlon2merc(&lat, &lon);

        return osmium::geom::Coordinates{lon, lat};
    }

    void target_to_tile(double *, double *) const override
    { /* nothing */
    }

    int target_srs() const override { return PROJ_SPHERE_MERC; }
    char const *target_desc() const override { return "Spherical Mercator"; }
};

} // anonymous namespace

std::shared_ptr<reprojection> reprojection::create_projection(int srs)
{
    switch (srs) {
    case PROJ_LATLONG:
        return std::make_shared<latlon_reprojection_t>();
    case PROJ_SPHERE_MERC:
        return std::make_shared<merc_reprojection_t>();
    }

    return make_generic_projection(srs);
}

void reprojection::coords_to_tile(double *tilex, double *tiley, double lon,
                                  double lat, int map_width)
{
    target_to_tile(&lat, &lon);

    *tilex = map_width * (0.5 + lon / EARTH_CIRCUMFERENCE);
    *tiley = map_width * (0.5 - lat / EARTH_CIRCUMFERENCE);
}
