#include "reprojection.hpp"

#include <osmium/geom/projection.hpp>

namespace {

/**
 * Generic projection using proj library.
 */
class generic_reprojection_t : public reprojection
{
public:
    explicit generic_reprojection_t(int srs) : m_target_srs(srs), pj_target(srs)
    {}

    osmium::geom::Coordinates reproject(osmium::Location loc) const override
    {
        double const lon = osmium::geom::deg_to_rad(loc.lon_without_check());
        double const lat = osmium::geom::deg_to_rad(loc.lat_without_check());

        return osmium::geom::transform(pj_source, pj_target,
                                       osmium::geom::Coordinates{lon, lat});
    }

    osmium::geom::Coordinates
    target_to_tile(osmium::geom::Coordinates coords) const override
    {
        return osmium::geom::transform(pj_target, pj_tile, coords);
    }

    int target_srs() const noexcept override { return m_target_srs; }

    char const *target_desc() const noexcept override
    {
        return pj_get_def(pj_target.get(), 0);
    }

private:
    int m_target_srs;
    osmium::geom::CRS pj_target;

    /// The projection of the source data. Always lat/lon (EPSG:4326).
    osmium::geom::CRS pj_source{PROJ_LATLONG};

    /**
     * The projection used for tiles. Currently this is fixed to be Spherical
     * Mercator. You will usually have tiles in the same projection as used
     * for PostGIS, but it is theoretically possible to have your PostGIS data
     * in, say, lat/lon but still create tiles in Spherical Mercator.
     */
    osmium::geom::CRS pj_tile{PROJ_SPHERE_MERC};
};

} // anonymous namespace

std::shared_ptr<reprojection> reprojection::make_generic_projection(int srs)
{
    return std::make_shared<generic_reprojection_t>(srs);
}
