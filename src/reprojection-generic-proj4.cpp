/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "format.hpp"
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

    geom::point_t reproject(geom::point_t point) const override
    {
        double const lon = osmium::geom::deg_to_rad(point.x());
        double const lat = osmium::geom::deg_to_rad(point.y());

        auto const c = osmium::geom::transform(
            pj_source, pj_target, osmium::geom::Coordinates{lon, lat});
        return {c.x, c.y};
    }

    geom::point_t target_to_tile(geom::point_t point) const override
    {
        auto const c = osmium::geom::transform(
            pj_target, pj_tile,
            osmium::geom::Coordinates{point.x(), point.y()});
        return {c.x, c.y};
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

std::string get_proj_version() { return "[API 4] {}"_format(pj_get_release()); }

