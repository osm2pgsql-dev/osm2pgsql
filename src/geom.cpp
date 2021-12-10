/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "geom.hpp"

#include <algorithm>

namespace geom {

bool operator==(point_list_t const &a, point_list_t const &b) noexcept
{
    return std::equal(a.cbegin(), a.cend(), b.cbegin(), b.cend());
}

bool operator!=(point_list_t const &a, point_list_t const &b) noexcept
{
    return !(a == b);
}

} // namespace geom
