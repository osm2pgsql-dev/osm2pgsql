#include "reprojection.hpp"

#include <osmium/geom/projection.hpp>

namespace {

/**
 * Generic projection based using proj library.
 */
class generic_reprojection_t : public reprojection
{
public:
    explicit generic_reprojection_t(int srs)
    : m_target_srs(srs), pj_target(srs), pj_source(PROJ_LATLONG),
      pj_tile(
          "+proj=merc +a=6378137 +b=6378137 +lat_ts=0.0 +lon_0=0.0 +x_0=0.0 "
          "+y_0=0 +k=1.0 +units=m +nadgrids=@null +wktext  +no_defs")
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
        auto c = transform(pj_target, pj_tile,
                           osmium::geom::Coordinates(*lon, *lat));

        *lon = c.x;
        *lat = c.y;
    }

    int target_srs() const override { return m_target_srs; }
    const char *target_desc() const override
    {
        return pj_get_def(pj_target.get(), 0);
    }

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

std::shared_ptr<reprojection> reprojection::make_generic_projection(int srs)
{
    return std::shared_ptr<reprojection>(new generic_reprojection_t{srs});
}
