/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "geom-box.hpp"

namespace geom {

box_t &box_t::extend(point_t const &point) noexcept
{
    if (point.x() < m_min_x) {
        m_min_x = point.x();
    }
    if (point.y() < m_min_y) {
        m_min_y = point.y();
    }
    if (point.x() > m_max_x) {
        m_max_x = point.x();
    }
    if (point.y() > m_max_y) {
        m_max_y = point.y();
    }

    return *this;
}

void box_t::extend(point_list_t const &list) noexcept
{
    for (auto const &point : list) {
        extend(point);
    }
}

void box_t::extend(box_t const &box) noexcept
{
    if (box.min_x() < m_min_x) {
        m_min_x = box.min_x();
    }
    if (box.min_y() < m_min_y) {
        m_min_y = box.min_y();
    }
    if (box.max_x() > m_max_x) {
        m_max_x = box.max_x();
    }
    if (box.max_y() > m_max_y) {
        m_max_y = box.max_y();
    }
}

box_t envelope(geom::nullgeom_t const & /*geom*/) { return box_t{}; }

box_t envelope(geom::point_t const &geom)
{
    box_t box;
    box.extend(geom);
    return box;
}

box_t envelope(geom::linestring_t const &geom)
{
    box_t box;
    box.extend(geom);
    return box;
}

box_t envelope(geom::polygon_t const &geom)
{
    box_t box;
    box.extend(geom.outer());
    return box;
}

box_t envelope(geom::multipoint_t const &geom)
{
    box_t box;
    for (auto const &point : geom) {
        box.extend(point);
    }
    return box;
}

box_t envelope(geom::multilinestring_t const &geom)
{
    box_t box;
    for (auto const &line : geom) {
        box.extend(line);
    }
    return box;
}

box_t envelope(geom::multipolygon_t const &geom)
{
    box_t box;
    for (auto const &polygon : geom) {
        box.extend(polygon.outer());
    }
    return box;
}

box_t envelope(geom::collection_t const &geom)
{
    box_t box;
    for (auto const &sgeom : geom) {
        box.extend(envelope(sgeom));
    }
    return box;
}

box_t envelope(geometry_t const &geom)
{
    return geom.visit([&](auto const &g) { return envelope(g); });
}

} // namespace geom
