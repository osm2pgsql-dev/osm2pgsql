#ifndef OSM2PGSQL_FLEX_EXPIRE_CONFIG_HPP
#define OSM2PGSQL_FLEX_EXPIRE_CONFIG_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <cstdlib>

enum class expire_mode
{
    full_area,     // Expire all tiles covered by polygon.
    boundary_only, // Expire only tiles covered by polygon boundary.
    hybrid // "full_area" or "boundary_only" mode depending on full_area_limit.
};

/**
 * These are the options used for tile expiry calculations.
 */
struct expire_config_t
{
    /**
     * The id of the tile set where expired tiles are collected.
     * Only used in the flex output.
     */
    std::size_t tileset = 0;

    /// Buffer around expired feature as fraction of the tile size.
    double buffer = 0.1;

    /**
     * Maximum width/heigth of bbox of a (multi)polygon before hybrid mode
     * expiry switches from full-area to boundary-only expire.
     */
    double full_area_limit = 0.0;

    /// Expire mode.
    expire_mode mode = expire_mode::full_area;

}; // struct expire_config_t

#endif // OSM2PGSQL_FLEX_EXPIRE_CONFIG_HPP
