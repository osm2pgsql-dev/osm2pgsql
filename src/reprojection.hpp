#ifndef OSM2PGSQL_REPROJECTION_HPP
#define OSM2PGSQL_REPROJECTION_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2025 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

/**
 * \file
 *
 * Contains the reprojection class.
 */

#include "geom.hpp"
#include "projection.hpp"

#include <memory>
#include <string>

/**
 * Virtual base class used for projecting OSM WGS84 coordinates into a
 * different coordinate system. Most commonly this will be used to convert
 * the coordinates into Spherical Mercator coordinates used in common web
 * tiles.
 */
class reprojection
{
public:
    reprojection() = default;

    reprojection(reprojection const &) = delete;
    reprojection &operator=(reprojection const &) = delete;

    reprojection(reprojection &&) = delete;
    reprojection &operator=(reprojection &&) = delete;

    virtual ~reprojection() = default;

    /**
     * Reproject from the source projection lat/lon (EPSG:4326)
     * to target projection.
     */
    virtual geom::point_t reproject(geom::point_t point) const = 0;

    /**
     * Converts coordinates from target projection to tile projection
     * (EPSG:3857)
     */
    virtual geom::point_t target_to_tile(geom::point_t point) const = 0;

    virtual int target_srs() const noexcept = 0;

    virtual char const *target_desc() const noexcept = 0;

    bool target_latlon() const noexcept { return target_srs() == PROJ_LATLONG; }

    /**
     * Create a reprojection object with target srs `srs`.
     *
     * The target projection (used in the PostGIS tables).
     * Controlled by the -l/-m/-E options.
     */
    static std::shared_ptr<reprojection> create_projection(int srs);

private:
    static std::shared_ptr<reprojection> make_generic_projection(int srs);
};

std::string get_proj_version();

/**
 * Get projection object for given srs. Objects are only created once and
 * then cached.
 */
reprojection const &get_projection(int srs);

#endif // OSM2PGSQL_REPROJECTION_HPP
