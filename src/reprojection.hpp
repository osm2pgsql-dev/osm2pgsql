#ifndef OSM2PGSQL_REPROJECTION_HPP
#define OSM2PGSQL_REPROJECTION_HPP

/**
 * \file
 *
 * This file is part of osm2pgsql (https://github.com/openstreetmap/osm2pgsql).
 *
 * It contains the reprojection class.
 */

#include <memory>

#include <osmium/geom/coordinates.hpp>
#include <osmium/osm/location.hpp>

enum Projection
{
    PROJ_LATLONG = 4326,
    PROJ_SPHERE_MERC = 3857
};

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
    virtual osmium::geom::Coordinates reproject(osmium::Location loc) const = 0;

    /**
     * Converts coordinates from target projection to tile projection
     * (EPSG:3857)
     */
    virtual osmium::geom::Coordinates
        target_to_tile(osmium::geom::Coordinates) const = 0;

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

#endif // OSM2PGSQL_REPROJECTION_HPP
