#include "format.hpp"
#include "reprojection.hpp"

#include <osmium/geom/coordinates.hpp>
#include <osmium/geom/util.hpp>

#include <proj.h>

namespace {

/**
 * Generic projection using proj library (version 6 and above).
 */
class generic_reprojection_t : public reprojection
{
public:
    explicit generic_reprojection_t(int srs)
    : m_target_srs(srs), m_context(proj_context_create())
    {
        assert(m_context);

        m_transformation = create_transformation(PROJ_LATLONG, srs);

        m_transformation.reset(proj_normalize_for_visualization(
            m_context.get(), m_transformation.get()));

        if (!m_transformation) {
            throw std::runtime_error{
                "Invalid projection '{}': {}"_format(srs, errormsg())};
        }

        m_transformation_tile = create_transformation(PROJ_SPHERE_MERC, srs);
    }

    osmium::geom::Coordinates reproject(osmium::Location loc) const override
    {
        return transform(m_transformation.get(),
                         osmium::geom::Coordinates{loc.lon_without_check(),
                                                   loc.lat_without_check()});
    }

    osmium::geom::Coordinates
    target_to_tile(osmium::geom::Coordinates coords) const override
    {
        return transform(m_transformation_tile.get(), coords);
    }

    int target_srs() const noexcept override { return m_target_srs; }

    char const *target_desc() const noexcept override { return ""; }

private:
    struct pj_context_deleter_t
    {
        void operator()(PJ_CONTEXT *ctx) const noexcept
        {
            proj_context_destroy(ctx);
        }
    };

    struct pj_deleter_t
    {
        void operator()(PJ *p) const noexcept { proj_destroy(p); }
    };

    char const *errormsg() const noexcept
    {
        return proj_errno_string(proj_context_errno(m_context.get()));
    }

    std::unique_ptr<PJ, pj_deleter_t> create_transformation(int from,
                                                            int to) const
    {
        std::string const source = "epsg:{}"_format(from);
        std::string const target = "epsg:{}"_format(to);

        std::unique_ptr<PJ, pj_deleter_t> trans{proj_create_crs_to_crs(
            m_context.get(), source.c_str(), target.c_str(), nullptr)};

        if (!trans) {
            throw std::runtime_error{
                "Invalid projection from {} to {}: {}"_format(from, to,
                                                              errormsg())};
        }
        return trans;
    }

    osmium::geom::Coordinates transform(PJ *transformation,
                                        osmium::geom::Coordinates coords) const
        noexcept
    {
        PJ_COORD c_in;
        c_in.lpzt.z = 0.0;
        c_in.lpzt.t = HUGE_VAL;
        c_in.lpzt.lam = osmium::geom::deg_to_rad(coords.x);
        c_in.lpzt.phi = osmium::geom::deg_to_rad(coords.y);

        auto const c_out = proj_trans(transformation, PJ_FWD, c_in);

        return osmium::geom::Coordinates{c_out.xy.x, c_out.xy.y};
    }

    int m_target_srs;
    std::unique_ptr<PJ_CONTEXT, pj_context_deleter_t> m_context;
    std::unique_ptr<PJ, pj_deleter_t> m_transformation;

    /**
     * The projection used for tiles. Currently this is fixed to be Spherical
     * Mercator. You will usually have tiles in the same projection as used
     * for PostGIS, but it is theoretically possible to have your PostGIS data
     * in, say, lat/lon but still create tiles in Spherical Mercator.
     */
    std::unique_ptr<PJ, pj_deleter_t> m_transformation_tile;
};

} // anonymous namespace

std::shared_ptr<reprojection> reprojection::make_generic_projection(int srs)
{
    return std::make_shared<generic_reprojection_t>(srs);
}

std::string get_proj_version()
{
    return "[API 6] {}"_format(proj_info().version);
}

