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
#include <numeric>

namespace geom {

void point_list_t::remove_duplicates()
{
    auto const it = std::unique(begin(), end());
    erase(it, end());
}

bool operator==(polygon_t const &a, polygon_t const &b) noexcept
{
    return (a.outer() == b.outer()) && (a.inners() == b.inners());
}

bool operator!=(polygon_t const &a, polygon_t const &b) noexcept
{
    return !(a == b);
}

std::size_t dimension(collection_t const &geom)
{
    return std::accumulate(geom.cbegin(), geom.cend(), 0ULL,
                           [](std::size_t max, auto const &member) {
                               return std::max(max, dimension(member));
                           });
}

std::size_t dimension(geometry_t const &geom)
{
    return geom.visit(
        overloaded{[&](auto const &input) { return dimension(input); }});
}

} // namespace geom
