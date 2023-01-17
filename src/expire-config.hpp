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

/**
 * These are the options used for tile expiry calculations.
 */
struct expire_config_t
{
    /// Buffer around expired feature as fraction of the tile size.
    double buffer = 0.1;

    /**
     * Maximum bbox of a (multi)polygon before expiry switches from full
     * polygon to boundary-only expire. Set to 0.0 to disable.
     */
    double max_bbox = 0.0;

    /// Expire only boundary of a (multi)polygon, not full polygon.
    bool boundary_only = false;

}; // struct expire_config_t

#endif // OSM2PGSQL_FLEX_EXPIRE_CONFIG_HPP
