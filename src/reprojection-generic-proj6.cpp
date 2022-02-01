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
    : m_target_srs(srs), m_context(proj_context_create()),
      m_transformation(create_transformation(PROJ_LATLONG, srs)),
      m_transformation_tile(create_transformation(srs, PROJ_SPHERE_MERC))
    {}

    geom::point_t
    reproject(geom::point_t point) const noexcept override
    {
        return transform(m_transformation.get(), point);
    }

    geom::point_t
    target_to_tile(geom::point_t point) const override
    {
        return transform(m_transformation_tile.get(), point);
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
        assert(m_context);

        std::string const source = "epsg:{}"_format(from);
        std::string const target = "epsg:{}"_format(to);

        std::unique_ptr<PJ, pj_deleter_t> trans{proj_create_crs_to_crs(
            m_context.get(), source.c_str(), target.c_str(), nullptr)};

        if (!trans) {
            throw std::runtime_error{
                "Invalid projection from {} to {}: {}"_format(from, to,
                                                              errormsg())};
        }

        std::unique_ptr<PJ, pj_deleter_t> trans_vis{
            proj_normalize_for_visualization(m_context.get(), trans.get())};

        if (!trans_vis) {
            throw std::runtime_error{
                "Invalid projection from {} to {}: {}"_format(from, to,
                                                              errormsg())};
        }

        return trans_vis;
    }

    geom::point_t transform(PJ *transformation,
                            geom::point_t point) const noexcept
    {
        PJ_COORD c_in;
        c_in.lpzt.z = 0.0;
        c_in.lpzt.t = HUGE_VAL;
        c_in.lpzt.lam = point.x();
        c_in.lpzt.phi = point.y();

        auto const c_out = proj_trans(transformation, PJ_FWD, c_in);

        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
        return {c_out.xy.x, c_out.xy.y};
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

