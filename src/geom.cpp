/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "geom.hpp"

namespace geom {

bool operator==(polygon_t const &a, polygon_t const &b) noexcept
{
    return (a.outer() == b.outer()) && (a.inners() == b.inners());
}

bool operator!=(polygon_t const &a, polygon_t const &b) noexcept
{
    return !(a == b);
}

} // namespace geom
