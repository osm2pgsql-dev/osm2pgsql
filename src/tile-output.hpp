#ifndef OSM2PGSQL_TILE_OUTPUT_HPP
#define OSM2PGSQL_TILE_OUTPUT_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "tile.hpp"

#include <iosfwd>

template <typename TChar, typename TTraits>
std::basic_ostream<TChar, TTraits> &
operator<<(std::basic_ostream<TChar, TTraits> &out, const tile_t &tile)
{
    return out << "TILE(" << tile.zoom() << ", " << tile.x() << ", " << tile.y()
               << ')';
}

#endif // OSM2PGSQL_TILE_OUTPUT_HPP
